#include "deadlock/async.h"

#include "sched.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include <unistd.h> /* sysconf */

/*
 * dl_default_sched is a singleton used by dlmain() and dlterminate().
 *
 * dl_this_worker is defined in worker.c and is used by dlasyncsz()
 * and dldefersz() to add work to the current worker queue.
 */
static               struct dlsched  *dl_default_sched = NULL;
extern _Thread_local struct dlworker *dl_this_worker;

void
dlasync(struct dltask *t)
{
    assert(dl_this_worker);
    dlworker_async(dl_this_worker, t);
}

void
dlcontinuation(struct dltask *this, struct dltask *next)
{
    if (this->next) {
        next->next = this->next;
        atomic_fetch_add_explicit(&this->next->wait, 1, memory_order_relaxed);
    }
}

int
dlmain(struct dltask *t, dlwentryfn wentryfn, dlwexitfn wexitfn)
{
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 0) return errno;
    if (ncpu == 0 || ncpu > INT_MAX) return errno = ERANGE;
    return dlmainex(t, wentryfn, wexitfn, (int)ncpu);
}

int
dlmainex(struct dltask *t, dlwentryfn wentryfn, dlwexitfn wexitfn, int workers)
{
    assert(!dl_default_sched);

    int result = 0;

    dl_default_sched = dlsched_alloc(workers);
    if (!dl_default_sched) {
        result = errno;
        goto malloc_failed;
    }

    result = dlsched_init(dl_default_sched, workers, t, wentryfn, wexitfn);
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
