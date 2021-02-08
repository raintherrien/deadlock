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
static dltask *dlworker_invoke(dltask *);
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
			t = dlworker_invoke(t);
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

static void
dlworker_entry(void *xworker)
{
	struct dlworker *w = xworker;

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
			t = dlworker_invoke(t);
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
dlworker_invoke(dltask *t)
{
	dltask *next = t->next_;
	t->fn_(t);
	if (next != NULL &&
	    1 == atomic_fetch_sub_explicit(&next->wait_, 1,
	                                   memory_order_release))
	{
		return next;
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
