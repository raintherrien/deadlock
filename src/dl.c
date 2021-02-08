#include "deadlock/dl.h"

#include "sched.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>

/* dl_default_sched is a singleton used by dlmain() and dlterminate(). */
static struct dlsched *dl_default_sched = NULL;

/*
 * dl_global_tid is declared in internal.h when profiling is enabled and is
 * used to semi-uniquely identify every task created
 */
atomic_uint dl_global_tid = 0;

void
dlasync(dltask *task, dltask *next)
{
	assert(dl_this_worker);
	assert(task);
	task->next_ = next;
	dlworker_async(dl_this_worker, task);
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

void
dldefer(dltask *task, unsigned count)
{
	assert(task);
	atomic_fetch_add_explicit(&task->wait_, count, memory_order_relaxed);
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
