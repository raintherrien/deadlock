#include "deadlock/dl.h"
#include "deadlock/graph.h"

#include "sched.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>

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
	assert(dl_this_worker);
	assert(task);
	task->fn_ = continuefn;
	if (task->next_) {
		atomic_fetch_add_explicit(&task->next_->wait_, 1,
		                          memory_order_relaxed);
	}

#if DEADLOCK_GRAPH_EXPORT
	dlworker_add_continuation_from_current(dl_this_worker, task);
#endif
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

	int result = 0;

	struct dlsched *sched = dlsched_alloc(workers);
	if (!sched) {
		result = errno;
		goto malloc_failed;
	}

	result = dlsched_init(sched, workers, task, entry, exit);
	if (result)
		goto dlsched_init_failed;

	dlsched_join(sched);
	dlsched_destroy(sched);
	free(sched);

	return 0;

dlsched_init_failed:
	free(sched);
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
dlswap(dltask *this, dltask *other)
{
	assert(dl_this_worker);
	assert(this && other);

	if (this->next_) {
		atomic_fetch_add_explicit(&this->next_->wait_, 1,
		                          memory_order_relaxed);
		dlnext(other, this->next_);
	}

	dlworker_async(dl_this_worker, other);
}

void
dltail(dltask *task)
{
	assert(dl_this_worker);
	assert(task);

	if (task->next_) {
		atomic_fetch_add_explicit(&task->next_->wait_, 1,
		                          memory_order_relaxed);
	}

	struct dlworker *w = dl_this_worker;
#if DEADLOCK_GRAPH_EXPORT
	dlworker_add_continuation_from_current(w, task);
#endif
	dlworker_async(w, task);
}

void
dlterminate(void)
{
	assert(dl_this_worker);
	dlsched_terminate(dl_this_worker->sched);
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
