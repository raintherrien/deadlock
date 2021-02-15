#include "deadlock/dl.h"
#include "deadlock/graph.h"

#include "sched.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>

/*
 * dl_default_sched is a singleton used by dlmain() and dlterminate().
 */
struct dlsched *dl_default_sched = NULL;

void
dlasync(dltask *task)
{
	assert(dl_this_worker);
	assert(task);
	struct dlworker *w = dl_this_worker;
#if DEADLOCK_GRAPH_EXPORT
	dlworker_add_edge_from_current(w, task);
#endif
	dlworker_async(w, task);
}

void
dlcontinuation(dltask *task, dltaskfn continuefn)
{
	assert(task);
	task->fn_ = continuefn;
	if (task->next_) {
		atomic_fetch_add_explicit(&task->next_->wait_, 1,
		                          memory_order_relaxed);
	}
}

int
dlmain(dltask *task, dlwentryfn entry, dlwexitfn exit)
{
	assert(task);
	errno = 0;
	int ncpu = dlprocessorcount();
	if (errno) return errno;
	return dlmainex(task, entry, exit, ncpu);
}

int
dlmainex(dltask *task, dlwentryfn entry, dlwexitfn exit, int workers)
{
	assert(task);
	assert(!dl_default_sched);

	int result = 0;

	dl_default_sched = dlsched_alloc(workers);
	if (!dl_default_sched) {
		result = errno;
		goto malloc_failed;
	}

	result = dlsched_init(dl_default_sched, workers, task, entry, exit);
	if (result) goto dlsched_init_failed;

	dlsched_join(dl_default_sched);
	dlsched_destroy(dl_default_sched);
	free(dl_default_sched);
	dl_default_sched = NULL;

	return 0;

dlsched_init_failed:
	free(dl_default_sched);
	dl_default_sched = NULL;

malloc_failed:
	return errno = result;
}

void
dlnext(dltask *task, dltask *next)
{
	assert(task);
	task->next_ = next;
}

void
dlterminate(void)
{
	assert(dl_default_sched);
	dlsched_terminate(dl_default_sched);
}

void
dlwait(dltask *task, unsigned wait)
{
	assert(task);
	atomic_fetch_add_explicit(&task->wait_, wait, memory_order_relaxed);
}

int
dlworker_index(void)
{
	assert(dl_this_worker);
	return dl_this_worker->index;
}
