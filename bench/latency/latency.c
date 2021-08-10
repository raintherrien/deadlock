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

static unsigned long num_threads;
static unsigned long num_tasks;

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
	struct timed_task timing[]; /* num_tasks in length */
};

static void master_task_run(DL_TASK_ARGS);
static void join_task_run(DL_TASK_ARGS);
static void spawn_task_run(DL_TASK_ARGS);
static void timed_task_run(DL_TASK_ARGS);

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

	struct master_task *master = malloc(sizeof(*master) + sizeof(struct timed_task) * num_tasks);
	if (master == NULL) {
		perror("Failed allocating tasks");
		return EXIT_FAILURE;
	}
	master->master = DL_TASK_INIT(master_task_run);
	master->iteration = 0;
	master->join_latency = 0;
	master->spawn_latency = 0;

	int result = dlmainex(&master->master, NULL, NULL, (int)num_threads);
	if (result) perror("Error in dlmain");

	free(master);

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
master_task_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct master_task, t, master);

	if (t->iteration < ITERATIONS) {
		++ t->iteration;
		dlcontinuation(&t->master, master_task_run);
		t->spawn_join = DL_TASK_INIT(spawn_task_run);
		dlwait(&t->master, 1);
		dlnext(&t->spawn_join, &t->master);
		dlasync(&t->spawn_join);
	} else {
		printf("Average latency of %lu tasks:\n"
		       "\tjoin:     %lluns\n"
		       "\tspawn:    %lluns\n",
		       num_tasks,
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
	time_ns avg_spawn_latency = 0;

	for (size_t i = 0; i < num_tasks; ++ i) {
		struct timed_task *tt = &t->timing[i];
		if (tt->completed > last_completed)
			last_completed = tt->completed;
		avg_spawn_latency += tt->completed - tt->scheduled;
	}

	t->join_latency  += joined - last_completed;
	t->spawn_latency += avg_spawn_latency / num_tasks;
}

static void
spawn_task_run(DL_TASK_ARGS)
{
	DL_TASK_ENTRY(struct master_task, t, spawn_join);

	dlcontinuation(&t->spawn_join, join_task_run);
	dlwait(&t->spawn_join, num_tasks);

	for (size_t i = 0; i < num_tasks; ++ i) {
		struct timed_task *tt = &t->timing[i];
		tt->task = DL_TASK_INIT(timed_task_run);
		dlnext(&tt->task, &t->spawn_join);
		tt->scheduled = now_ns();
		dlasync(&tt->task);
	}
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
