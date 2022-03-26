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

/*
 * Very basic timing
 */
typedef unsigned long long time_ns;
static time_ns now_ns(void);

static int num_threads;

struct timed_task {
	/* _Alignas(128) non-destructive; this isn't even fair :) */
	dltask task;
	time_ns scheduled;
	time_ns completed;
};

struct master_task {
	dltask master;
	dltask spawn_join;
	unsigned iteration;
	time_ns join_latency;
	time_ns spawn_latency;
	struct timed_task timing;
};

static void master_task_run(DL_TASK_ARGS);
static void join_task_run(DL_TASK_ARGS);
static void spawn_task_run(DL_TASK_ARGS);
static void timed_task_run(DL_TASK_ARGS);

int
main(int argc, char **argv)
{
	if (argc > 1 && argv[1]) {
		errno = 0;
		num_threads = (int)strtoul(argv[1], NULL, 10);
		if (num_threads == 0) errno = EINVAL;
		if (errno) {
			perror("Invalid <num-threads>");
			goto print_usage;
		}

		if ((int)num_threads < 2) {
			perror("<num-threads> must be 2 or more to measure latency");
			goto print_usage;
		}
	} else {
		num_threads = -1;
	}

	struct master_task *master = malloc(sizeof(*master));
	if (master == NULL) {
		perror("Failed allocating tasks");
		return EXIT_FAILURE;
	}
	master->master = dlcreate(master_task_run, NULL);
	master->iteration = 0;
	master->join_latency = 0;
	master->spawn_latency = 0;

	int result;
	if (num_threads == -1) {
		result = dlmain(&master->master, NULL, NULL);
	} else {
		result = dlmainex(&master->master, NULL, NULL, num_threads);
	}
	if (result) perror("Error in dlmain");

	free(master);

	return result;

print_usage:
	fprintf(stderr, "Usage: ./latency <num-threads>\n");
	return EXIT_SUCCESS;
}

static void
master_task_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct master_task, t, master);

	if (t->iteration < ITERATIONS) {
		++ t->iteration;
		dlrecapture(&t->master, master_task_run);
		t->spawn_join = dlcreate(spawn_task_run, &t->master);
		dldetach(&t->spawn_join);
		dldetach(&t->master);
	} else {
		printf("Average latency of %lu tasks:\n"
		       "\tjoin:     %lluns\n"
		       "\tspawn:    %lluns\n",
		       (unsigned long)ITERATIONS,
		       t->join_latency / ITERATIONS,
		       t->spawn_latency / ITERATIONS);
		dlterminate();
	}
}

static void
join_task_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct master_task, t, spawn_join);

	time_ns joined = now_ns();
	time_ns last_completed = 0;

	if (t->timing.completed > last_completed)
		last_completed = t->timing.completed;
	t->spawn_latency += t->timing.completed - t->timing.scheduled;
	t->join_latency += joined - last_completed;
}

static void
spawn_task_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct master_task, t, spawn_join);

	dlrecapture(&t->spawn_join, join_task_run);
	t->timing.task = dlcreate(timed_task_run, &t->spawn_join);
	t->timing.scheduled = now_ns();
	dldetach(&t->timing.task);
	dldetach(&t->spawn_join);
}

static void
timed_task_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct timed_task, t, task);
	t->completed = now_ns();
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
