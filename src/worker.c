#include "worker.h"
#include "sched.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <immintrin.h> /* _mm_pause */

_Thread_local struct dlworker *dl_this_worker;

/*
 * dlworker_entry() is the main loop of each worker thread. It spins until the
 * scheduler signals termination, popping work from the local queue and
 * attempting to steal from other work queues when the local work dries up.
 *
 * dlworker_invoke() invokes a task and returns that tasks' next pointer if it
 * is ready to be invoked.
 *
 * dlworker_stall() blocks until more tasks are queued.
 */
static void    dlworker_entry (void*);
static dltask *dlworker_invoke(struct dlworker *, dltask *);
static void    dlworker_stall (struct dlworker *);

void
dlworker_async(struct dlworker *w, dltask *t)
{
	do {
		/*
		 * dltqueue_push shall only return success or ENOBUFS.
		 * If there is no space execute this task immediately.
		 */
		switch (dltqueue_push(&w->tqueue, t)) {
		case 0: {
			int result = dlwait_signal(&w->sched->stall);
			if (result == 0) return;
			errno = result;
			perror("dlworker_async failed to signal stall");
			exit(errno);
		}
		case ENOBUFS:
			t = dlworker_invoke(w, t);
		}
	} while (t);
}

void
dlworker_destroy(struct dlworker *w)
{
	dltqueue_destroy(&w->tqueue);
}

void
dlworker_join(struct dlworker *w)
{
	if ((errno = dlthread_join(&w->thread))) {
		perror("dlworker_join failed to join worker thread");
		exit(errno);
	}
}

int
dlworker_init(struct dlworker *w, struct dlsched *s, dltask *task,
              dlwentryfn entry, dlwexitfn exit, int index)
{
	int result = 0;

	w->sched = s;
	w->entry = entry;
	w->exit  = exit;
	w->index = index;

#if DEADLOCK_GRAPH_EXPORT
	w->current_graph = NULL;
#endif

	/* TODO: Hardcoded task capacity */
	unsigned int initsz = 8192; /* 8192 * 8B = 64KiB */

	result = dltqueue_init(&w->tqueue, initsz);
	if (result) goto tqueue_init_failed;

	if (task) {
		result = dltqueue_push(&w->tqueue, task);
		if (result) goto tqueue_prime_failed;
	}

	/* TODO: Thread pinning */
	result = dlthread_create(&w->thread, dlworker_entry, w);
	if (result) goto pthread_create_failed;

	return 0;

pthread_create_failed:
tqueue_prime_failed:
	dltqueue_destroy(&w->tqueue);
tqueue_init_failed:
	return errno = result;
}

#if DEADLOCK_GRAPH_EXPORT

void
dlworker_set_current_node(void *wx, unsigned long description)
{
	struct dlworker *w = wx;
	w->current_node = (struct dlgraph_node) {
		.begin_ns = dlgraph_now(),
		.task = w->invoked_task_id,
		.desc = description,
		.label_offset = ULONG_MAX
	};
}

void
dlworker_add_current_node(void *wx)
{
	struct dlworker *w = wx;
	dlgraph_add_node(w->current_graph->fragments + w->index, &w->current_node);
}

void
dlworker_add_continuation_from_current(void *wx, dltask *task)
{
	struct dlworker *w = wx;
	struct dlgraph *graph = w->current_graph;
	if (graph) {
		dlgraph_add_continuation(graph->fragments + w->index, w->invoked_task_id, task->tid_);
	}
}

void
dlworker_add_edge_from_current(void *wx, dltask *task)
{
	struct dlworker *w = wx;
	struct dlgraph *graph = w->current_graph;
	if (graph) {
		task->graph_ = graph;
		dlgraph_add_edge(graph->fragments + w->index, w->invoked_task_id, task->tid_);
	}
}

#endif

static void
dlworker_entry(void *xworker)
{
	struct dlworker *w = xworker;

	/*
	 * Initialize this threads task ID generator's most significant byte
	 * with this workers ID (+ 1 to allow for tasks initialized without a
	 * worker).
	 */
#if DEADLOCK_GRAPH_EXPORT
	dl_next_task_id = ((unsigned long)(w->index+1) << 24) & 0xFF000000;
#endif

	/* Thread local pointer to this worker used by async etc. */
	dl_this_worker = w;

	/* Invoke the entry callback */
	if (w->entry) w->entry(w->index);

	/* Synchronize all workers before they start stealing */
	atomic_fetch_sub(&w->sched->wbarrier, 1);
	while(atomic_load(&w->sched->wbarrier) > 0 &&
	      !atomic_load(&w->sched->terminate))
	{
		dlthread_yield();
	}

	/* Work loop */
	dltask *t = NULL;
	while (!atomic_load_explicit(&w->sched->terminate,
	          memory_order_relaxed))
	{
		if (t) {
			invoke:
			t = dlworker_invoke(w, t);
			continue;
		}

		/* take local task */
		take: ;
		int rc = dltqueue_take(&w->tqueue, &t);
		if (rc == EAGAIN) {
			_mm_pause();
			goto take;
		} else if (rc == 0) {
			goto invoke;
		}
		assert(rc == ENODATA);

		/* attempt to steal 16 times before stalling */
		for (size_t sc = 0; sc < 16; ++ sc) {
			int rc = dlsched_steal(w->sched, &t, w->index);
			if (rc == 0) goto invoke;
			assert(rc == ENODATA);
			dlthread_yield();
		}
		t = NULL;
		dlworker_stall(w);
	}

	/* Invoke the exit lifetime callback */
	if (w->exit) w->exit(w->index);

	/* Synchronize workers until they're all joinable */
	atomic_fetch_add(&w->sched->wbarrier, 1);
}

static dltask *
dlworker_invoke(struct dlworker *w, dltask *t)
{
	(void)w;

	assert(atomic_load_explicit(&t->wait_, memory_order_relaxed) == 0);
	dltask *next = t->next_;

#if DEADLOCK_GRAPH_EXPORT
	w->current_graph = t->graph_;
	w->invoked_task_id = dltask_xchg_id(t);
#endif

	t->fn_(w, t);

	/* Propegate graph to child and add this completed node to graph. */
#if DEADLOCK_GRAPH_EXPORT
	struct dlgraph *graph = w->current_graph;
	if (graph) {
		dlworker_add_current_node(w);
		if (next)
			dlworker_add_edge_from_current(w, next);
	}
#endif

	if (next) {
		unsigned wait = atomic_fetch_sub_explicit(&next->wait_, 1,
		                                          memory_order_release);
		switch (wait) {
		case 0:
			errno = EINVAL;
			perror("dlworker_invoke next task invalid wait count");
			exit(errno);
		case 1:
			return next;
		}
	}
	return NULL;
}

static void
dlworker_stall(struct dlworker *w)
{
	int pr = dlwait_wait(&w->sched->stall);
	if (pr) {
		errno = pr;
		perror("dlworker_stall failed to dlwait_wait on stall");
		exit(errno);
	}
}
