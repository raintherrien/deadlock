#include "deadlock/async.h"
#include "optick_capi.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <time.h> /* clock_gettime */
#endif

/*
 * worker_entry is responsible for setting up a thread context for
 * profiling before any tasks are executed.
 */

static void worker_entry(int);

/*
 * terminte_async terminates the scheduler and dumps our Optick capture.
 */
static void terminate_async(struct dltask *);
static struct dltask terminate_task = {
    .fn = terminate_async,
    .next = NULL
};

/*
 * This example runs for a set number of rounds, each round setting a
 * random target in the range [0..NTASKS), then kicks off NTASKS child
 * tasks, each of which concurrently increment the global guess.
 * Whichever task guesses correctly increments its own progress meter by
 * one. Once all NTASKS are complete, a new round begins.
 *
 * TODO: Announce winner(s)
 *
 * set_next_goal_and_fork_async sets the new guess and spawns NTASKS
 * child tasks.
 *
 * make_guess_and_advance_async is the child task which makes a guess.
 */
#define ROUNDS 8
#define NTASKS 4096
static unsigned int progress[NTASKS] = { 0 };
static atomic_uint  guess  = 0;
static unsigned int target = 0;
static unsigned int roundn = 0;

static void set_next_goal_and_fork_async(struct dltask *xargs);
static struct dlnext set_next_goal_and_fork_task = {
    .task = {
        .fn = set_next_goal_and_fork_async,
        .next = NULL
    }
};

static void make_guess_and_advance_async(struct dltask *xargs);
static struct make_guess_and_advance_task {
    struct dltask dltask;
    size_t id;
} *make_guess_and_advance_tasks;

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
     * Allocate and initialize our child tasks.
     */
    make_guess_and_advance_tasks = calloc(NTASKS, sizeof *make_guess_and_advance_tasks);
    if (!make_guess_and_advance_tasks) {
        perror("Failed to allocate tasks");
        return EXIT_FAILURE;
    }
    for (size_t i = 0; i < NTASKS; ++ i) {
        make_guess_and_advance_tasks[i] = (struct make_guess_and_advance_task) {
            .dltask = (struct dltask) {
                .fn = make_guess_and_advance_async,
                .next = &set_next_goal_and_fork_task
            },
            .id = i
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
    int result = dlmainex(&set_next_goal_and_fork_task.task, worker_entry, NULL, (int)num_threads);
    if (result) perror("Error in dlmain");

    free(make_guess_and_advance_tasks);

    return result;
}

static void
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

static void
terminate_async(struct dltask *xargs)
{
    (void) xargs;
    const char *optfn = "fork-join";
    OptickAPI_StopCapture(optfn, (uint16_t)strlen(optfn));
    dlterminate();
}

static void
set_next_goal_and_fork_async(struct dltask *xargs)
{
    (void) xargs; /* ignore set_next_goal_and_fork_task */

    OptickAPI_NextFrame();
    BGN_EVENT;

    //struct timespec t0, t1;
    //clock_gettime(CLOCK_REALTIME, &t0);

    printf("Beginning round %u\n", roundn);
    if (++ roundn == ROUNDS) {
        dlasync(&terminate_task);
    } else {
        /* Reset wait counter */
        atomic_store_explicit(&set_next_goal_and_fork_task.wait, NTASKS, memory_order_relaxed);

        target = rand() % NTASKS;
        for (size_t i = 0; i < NTASKS; ++ i) {
            dlasync(&make_guess_and_advance_tasks[i].dltask);
        }
    }

    //clock_gettime(CLOCK_REALTIME, &t1);
    //unsigned long total_time_ns = (t1.tv_sec-t0.tv_sec) * 1000000000 + t1.tv_nsec-t0.tv_nsec;
    //printf("\tset_next_goal_and_fork_async took: %luns\n", total_time_ns);
    //printf("\tapprox. time per task: %luns\n", total_time_ns / NTASKS);

    END_EVENT;
}

static void
make_guess_and_advance_async(struct dltask *xargs)
{
    BGN_EVENT;

    struct make_guess_and_advance_task *this = (struct make_guess_and_advance_task *)xargs;
    unsigned int this_guess = atomic_fetch_add(&guess, 1);
    if (this_guess == target) {
        ++ progress[this->id];
    }

    /*
     * If tasks complete too quickly, especially with a high number of
     * worker threads, contention on the task queues as well as the cost
     * of waking stalled workers will cause runtimes to balloon.
     *
     * In real code this isn't an issue. :) I find 10ns+overhead is long
     * enough for set_next_goal_and_fork_async to queue up NTASKS for
     * the 32 hardware threads of my 3950x to fight over without too
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
