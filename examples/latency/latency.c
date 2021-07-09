#include "deadlock/dl.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h> /* clock_gettime */
#endif

#define ITERATIONS 8192u

typedef unsigned long long time_ns;

static unsigned long num_threads;
static unsigned long num_tasks;
static _Atomic(time_ns) total_latency_sum = 0;;
static _Atomic(unsigned long) total_complete_count = 0;

_Thread_local time_ns tl_latency_sum;

static void worker_entry(int);
static void worker_exit(int);

struct spawn_task {
	dltask task;
};

struct timed_task {
	_Alignas(128) /* non-destructive */
	dltask task;
	time_ns scheduled;
};

static void spawn_task_run(DL_TASK_ARGS);
static void timed_task_run(DL_TASK_ARGS);

static time_ns now_ns(void);

int
main(int argc, char **argv)
{
	if (argc < 3 || argv[1] == NULL || argv[2] == NULL)
		goto print_usage;

	errno = 0;
	num_threads = strtoul(argv[1], NULL, 10);
	if (num_threads == 0) errno = EINVAL;
	if (errno) {
		perror("Invalid <num-threads>");
		goto print_usage;
	}

	if ((int)num_threads < 2) {
		perror("<num-threads> must be 2 or more to measure latency");
		goto print_usage;
	}

	num_tasks = strtoul(argv[2], NULL, 10);
	if (num_tasks == 0) errno = EINVAL;
	if (errno || (int)num_tasks < 1) {
		perror("Invalid <num-tasks>");
		goto print_usage;
	}

	printf("Spawning %lu threads\n", num_threads);
	printf("Measuring task contention/latency by spawning %lu tasks\n", num_tasks);

	struct spawn_task spawner = { .task = DL_TASK_INIT(spawn_task_run) };

	int result = dlmainex(&spawner.task, worker_entry, worker_exit, (int)num_threads);
	if (result) perror("Error in dlmain");

	float avg_us = total_latency_sum / 1000.0f / total_complete_count;
	printf("Average latency of %lu tasks: %fus\n", total_complete_count, avg_us);

	return result;

print_usage:
	fprintf(stderr, "Usage: ./latency <num-threads> <num-tasks>\n"
	                " <num-threads> worker threads are created and the application"
	                " spawns <num-tasks> tasks for them to fight over. Latency is"
	                " measured between when the task is spawned and when it is"
	                " stolen by a worker thread. The spawning thread blocks and"
	                " does *not* perform its own tasks.\n");
	return EXIT_SUCCESS;
}

static void
worker_entry(int wid)
{
	(void) wid;
	tl_latency_sum = 0;
}

static void
worker_exit(int wid)
{
	(void) wid;
	total_latency_sum += tl_latency_sum;
}

static void
spawn_task_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct spawn_task, t, task);

	struct timed_task *ts = malloc(num_tasks * sizeof(*ts));
	if (ts == NULL) {
		perror("Failed to allocate timed tasks");
		dlterminate();
		return;
	}

	/* Not we BLOCK here rather than using dltail */
	for (unsigned iteration = 0; iteration < ITERATIONS; ++ iteration) {
		for (size_t ti = 0; ti < num_tasks; ++ ti) {
			ts[ti] = (struct timed_task) {
				.task = DL_TASK_INIT(timed_task_run),
				.scheduled = now_ns()
			};
			dlasync(&ts[ti].task);
		}

		while (total_complete_count < (iteration+1)*num_tasks) {
			/*
			 * Busy wait on the atomic, which is definitely
			 * worst case :)
			 */
		}
	}

	free(ts);

	dlterminate();
}

static void
timed_task_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct timed_task, t, task);
	tl_latency_sum += now_ns() - t->scheduled;
	++ total_complete_count;
}

static time_ns
now_ns(void)
{
	struct timespec t;
#if _POSIX_C_SOURCE >= 199309L
	clock_gettime(CLOCK_MONOTONIC, &t);
#else
	timespec_get(&t, TIME_UTC);
#endif
	return t.tv_sec * 1000000000ull + t.tv_nsec;
}
