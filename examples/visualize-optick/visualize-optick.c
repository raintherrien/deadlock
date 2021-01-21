#include "deadlock/async.h"
#include "optick_capi.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <time.h> /* clock_gettime */
#endif

/*
 * This example runs for a set number of rounds, each round setting a
 * random target in the range [0..NUM_CONTESTANTS), then forks
 * NUM_CONTESTANTS child tasks, each of which concurrently increment the
 * global guess variable.
 *
 * Whichever task guesses correctly increments its own score by one.
 * Once all NUM_CONTESTANTS join, a new round begins.
 */
#define NUM_ROUNDS      8
#define NUM_CONTESTANTS 4096

/*
 * Responsible for setting up a thread context for profiling before any
 * tasks are executed.
 */
void worker_entry(int);

struct contestant_pkg;
struct game_pkg;
struct terminate_pkg;

/*
 * Announces a winner, terminates the scheduler, and dumps our capture.
 */
struct terminate_pkg {
    struct dltask task;
    unsigned int  winner;
};
void terminate_run(struct dltask *);

/*
 * Performs a guess.
 */
struct contestant_pkg {
    struct dltask    task;
    struct game_pkg *game;
    unsigned int     score;
};
void contestant_run(struct dltask *);

/*
 * Orchestrates the whole mess.
 */
struct game_pkg {
    struct dltask         task;
    atomic_uint           guess;
    unsigned int          target;
    unsigned int          round;
    struct terminate_pkg  terminate;
    struct contestant_pkg contestants[];
};
void game_run(struct dltask *);

/*
 * Helper macros to scope Optick events
 */
#define BGN_EVENT do {                                 \
    uint64_t edesc = OptickAPI_CreateEventDescription( \
        __func__, strlen(__func__),                    \
        __FILE__, strlen(__FILE__),                    \
        __LINE__);                                     \
    uint64_t edata = OptickAPI_PushEvent(edesc)

#define END_EVENT OptickAPI_PopEvent(edata); } while (0)

int
main(int argc, char **argv)
{
    /*
     * Parse runtime args BEFORE handing off to scheduler because we
     * want to explicitly set the number of workers on the command line
     * using dlmainex() rather than dlmain(), which spawns one worker
     * per hardware thread.
     */
    unsigned long num_threads = 0;
    switch (argc) {
    case 0:
    case 1:
        fprintf(stderr, "Usage: ./fork-join <num-threads>\n");
        return EXIT_SUCCESS;
    default:
        errno = 0;
        num_threads = strtoul(argv[1], NULL, 10);
        if (num_threads == 0) errno = EINVAL;
        if (errno != 0) {
            fprintf(stderr, "Usage: ./fork-join <num-threads>\n");
            perror("Invalid <num-threads>");
            return EXIT_FAILURE;
        }
        printf("Spawning %ld worker threads\n", num_threads);
    }

    /*
     * Allocate and initialize our game state. game_pkg is a recursive
     * task which executes NUM_ROUNDS times.
     */
    struct game_pkg *game = malloc(sizeof *game + sizeof(struct contestant_pkg) * NUM_CONTESTANTS);
    if (!game) {
        perror("Failed to allocate game state");
        return EXIT_FAILURE;
    }
    *game = (struct game_pkg) {
        .task = (struct dltask) {
            .fn = game_run,
            .next = &game->task
        },
        .round = 0
    };

    /*
     * Initialize contestants
     */
    for (unsigned int i = 0; i < NUM_CONTESTANTS; ++ i) {
        game->contestants[i] = (struct contestant_pkg) {
            .task = (struct dltask) {
                .fn = contestant_run,
                .next = &game->task
            },
            .game = game,
            .score = 0
        };
    }

    /*
     * Begin profiling. Terminated in terminate_async.
     */
    OptickAPI_RegisterThread("MainThread", 10);
    OptickAPI_StartCapture();

    /*
     * Transfer complete control to a Deadlock scheduler. This function
     * will return when our scheduler is terminated.
     */
    int result = dlmainex(&game->task, worker_entry, NULL, (int)num_threads);
    if (result) perror("Error in dlmain");

    free(game);

    return result;
}

void
worker_entry(int id)
{
    char threadname[16] = { '\0' };
#ifdef _WIN32
    sscanf_s(threadname, "Worker %d", &id);
#else
    sscanf(threadname, "Worker %d", &id);
#endif
    OptickAPI_RegisterThread(threadname, (uint16_t)strlen(threadname));
}

void
terminate_run(struct dltask *xtask)
{
    struct terminate_pkg *pkg = dldowncast(xtask, struct terminate_pkg, task);
    printf("Congratulations contestant %u!\n", pkg->winner);
    const char *optfn = "fork-join";
    OptickAPI_StopCapture(optfn, (uint16_t)strlen(optfn));
    dlterminate();
}

void
game_run(struct dltask *xtask)
{
    struct game_pkg *pkg = dldowncast(xtask, struct game_pkg, task);

    OptickAPI_NextFrame();
    BGN_EVENT;

    printf("Beginning round %u\n", pkg->round);
    if (++ pkg->round == NUM_ROUNDS) {
        /*
         * Cease game graph recursion. Determine winner and terminate.
         */
        pkg->terminate = (struct terminate_pkg) {
            .task = (struct dltask) { .fn = terminate_run },
            .winner = UINT_MAX
        };
        unsigned int highest_score = 0;
        for (unsigned int i = 0; i < NUM_CONTESTANTS; ++ i) {
            if (pkg->contestants[i].score > highest_score) {
                pkg->contestants[i].score = highest_score;
                pkg->terminate.winner = i;
            }
        }
        dlasync(&pkg->terminate.task);
    } else {
        /*
         * Reset wait counter on this recursive graph
         * TODO: dljoin
         */
        atomic_store(&pkg->task.wait, NUM_CONTESTANTS);

        /*
         * Set new target and fork children
         */
        pkg->target = rand() % NUM_CONTESTANTS;
        for (unsigned int i = 0; i < NUM_CONTESTANTS; ++ i) {
            dlasync(&pkg->contestants[i].task);
        }
    }

    END_EVENT;
}

void
contestant_run(struct dltask *xtask)
{
    struct contestant_pkg *pkg = dldowncast(xtask, struct contestant_pkg, task);

    BGN_EVENT;

    unsigned int this_guess = atomic_fetch_add(&pkg->game->guess, 1);
    if (this_guess == pkg->game->target) {
        ++ pkg->score;
    }

    /*
     * If tasks complete too quickly, especially with a high number of
     * worker threads, contention on the task queues as well as the cost
     * of waking stalled workers will cause runtimes to balloon.
     *
     * In real code this isn't an issue. :) I find 10ns+overhead is long
     * enough for set_next_goal_and_fork_async to queue up NUM_CONTESTANTS
     * for the 32 hardware threads of my 3950x to fight over without too
     * much stalling.
     */
#ifdef _WIN32
    Sleep(0);
#else
    struct timespec timeout = { .tv_sec = 0, .tv_nsec = 10 };
    (void) nanosleep(&timeout, NULL);
#endif

    END_EVENT;
}
