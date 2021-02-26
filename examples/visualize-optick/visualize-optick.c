#include "deadlock/dl.h"
#include "optick_capi.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <synchapi.h> /* Sleep */
#else
#include <time.h> /* nanosleep */
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
	dltask task;
};
static DL_TASK_DECL(terminate_run);

/*
 * Performs a guess.
 */
struct contestant_pkg {
	dltask    task;
	struct game_pkg *game;
	unsigned int     score;
};
static DL_TASK_DECL(contestant_run);

/*
 * Orchestrates the whole mess.
 */
struct game_pkg {
	dltask         task;
	atomic_uint           guess;
	unsigned int          target;
	unsigned int          round;
	unsigned int          winner;
	struct contestant_pkg contestants[];
};
static DL_TASK_DECL(game_start);
static DL_TASK_DECL(game_round);

/*
 * Helper macros to scope Optick events
 */
#define BGN_EVENT do {                                     \
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
	 * want to explicitly set the number of workers on the command
	 * line using dlmainex() rather than dlmain(), which spawns one
	 * worker per hardware thread.
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
		printf("Spawning %lu worker threads\n", num_threads);
	}

	struct terminate_pkg terminate = (struct terminate_pkg) {
		.task = DL_TASK_INIT(terminate_run)
	};

	/*
	 * Allocate and initialize our game state. game_pkg is a
	 * recursive task which executes NUM_ROUNDS times.
	 */
	struct game_pkg *game = malloc(sizeof *game +
	                               sizeof(struct contestant_pkg) *
	                                 NUM_CONTESTANTS);
	if (!game) {
		perror("Failed to allocate game state");
		return EXIT_FAILURE;
	}
	*game = (struct game_pkg) {
		.task = DL_TASK_INIT(game_start),
		.round = 0,
		.winner = UINT_MAX
	};

	dlnext(&game->task, &terminate.task);
	dlwait(&terminate.task, 1);

	/*
	 * Initialize contestants
	 */
	for (unsigned int i = 0; i < NUM_CONTESTANTS; ++ i) {
		game->contestants[i] = (struct contestant_pkg) {
			.task = DL_TASK_INIT(contestant_run),
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
	 * Transfer complete control to a Deadlock scheduler. This
	 * function will return when our scheduler is terminated.
	 */
	int result = dlmainex(&game->task, worker_entry, NULL, (int)num_threads);
	if (result) perror("Error in dlmain");

	free(game);

	return result;
}

void
worker_entry(int id)
{
	const size_t threadname_len = 16;
	char threadname[threadname_len];
	snprintf(threadname, threadname_len, "Worker %d", id);
	OptickAPI_RegisterThread(threadname, (uint16_t)strlen(threadname));
}

static DL_TASK_DECL(terminate_run)
{
	DL_TASK_ENTRY(struct terminate_pkg, pkg, task);
	const char *optfn = "fork-join";
	OptickAPI_StopCapture(optfn, (uint16_t)strlen(optfn));
	dlterminate();
}

static DL_TASK_DECL(game_start)
{
	DL_TASK_ENTRY(struct game_pkg, pkg, task);
	BGN_EVENT;
	dlcontinuation(&pkg->task, game_round);
	dlasync(&pkg->task);
	END_EVENT;
}

static DL_TASK_DECL(game_round)
{
	DL_TASK_ENTRY(struct game_pkg, pkg, task);

	OptickAPI_NextFrame();
	BGN_EVENT;

	printf("Beginning round %u\n", pkg->round);
	if (++ pkg->round == NUM_ROUNDS) {
		/*
		 * Cease game graph recursion. Determine winner and terminate.
		 */
		unsigned int highest_score = 0;
		for (unsigned int i = 0; i < NUM_CONTESTANTS; ++ i) {
			if (pkg->contestants[i].score > highest_score) {
				pkg->contestants[i].score = highest_score;
				pkg->winner = i;
			}
		}
		printf("Congratulations contestant %u!\n", pkg->winner);
	} else {
		/*
		 * Set new target and fork children
		 */
		dlcontinuation(&pkg->task, game_round);
		dlwait(&pkg->task, NUM_CONTESTANTS);
		pkg->target = rand() % NUM_CONTESTANTS;
		for (unsigned int i = 0; i < NUM_CONTESTANTS; ++ i) {
			dlnext(&pkg->contestants[i].task, &pkg->task);
			dlasync(&pkg->contestants[i].task);
		}
	}

	END_EVENT;
}

static DL_TASK_DECL(contestant_run)
{
	DL_TASK_ENTRY(struct contestant_pkg, pkg, task);

	BGN_EVENT;

	unsigned int this_guess = atomic_fetch_add(&pkg->game->guess, 1);
	if (this_guess == pkg->game->target) {
		++ pkg->score;
	}

	/*
	 * If tasks complete too quickly, especially with a high number
	 * of worker threads, contention on the task queues as well as
	 * the cost of waking stalled workers will cause runtimes to
	 * balloon.
	 *
	 * In real code this isn't an issue. :) I find 10ns+overhead is
	 * long enough for set_next_goal_and_fork_async to queue up
	 * NUM_CONTESTANTS for the 32 hardware threads of my 3950x to
	 * fight over without too much stalling.
	 */
#ifdef _WIN32
	Sleep(0);
#else
	struct timespec timeout = { .tv_sec = 0, .tv_nsec = 10 };
	(void) nanosleep(&timeout, NULL);
#endif

	END_EVENT;
}
