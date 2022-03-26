#include "deadlock/dl.h"
#include "deadlock/graph.h"

#include "sched.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>

dltask
dlcreate(dltaskfn fn, dltask *next)
{
	if (next) {
		atomic_fetch_add(&next->wait_, 1);
	}

	return (dltask) {
		.next_ = next,
		.fn_ = fn,
		.wait_ = 1,
#ifdef DEADLOCK_GRAPH_EXPORT
		.tid_ = dltask_next_id()
#endif
	};
}

void
dldetach(dltask *task)
{
	assert(task);

	unsigned w = atomic_fetch_sub(&task->wait_, 1);
	assert(w > 0);
	if (w == 1) {
		assert(dl_this_worker);
		struct dlworker *w = dl_this_worker;
#ifdef DEADLOCK_GRAPH_EXPORT
		dlworker_add_edge_from_current(w, task);
#endif
		dlworker_async(w, task);
	}
}

void
dlrecapture(dltask *task, dltaskfn continuefn)
{
	assert(dl_this_worker);
	assert(task);
	atomic_fetch_add(&task->wait_, 1);
	task->fn_ = continuefn;
	if (task->next_) {
		atomic_fetch_add(&task->next_->wait_, 1);
	}

#ifdef DEADLOCK_GRAPH_EXPORT
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

	unsigned w = atomic_fetch_sub(&task->wait_, 1);
	assert(w == 1);

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
dlterminate(void)
{
	assert(dl_this_worker);
	dlsched_terminate(dl_this_worker->sched);
}

int
dlworker_index(void)
{
	assert(dl_this_worker);
	return dl_this_worker->index;
}
