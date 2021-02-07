#include "sched.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <immintrin.h> /* _mm_pause */

void *
dlsched_alloc(int nworkers)
{
    if (nworkers < 0) {
        errno = ERANGE;
        return NULL;
    }
    /* Workers in dlsched are flexible array member */
    return malloc(sizeof(struct dlsched) +
                  sizeof(struct dlworker) * (unsigned)nworkers);
}

void
dlsched_destroy(struct dlsched *s)
{
    assert(s->wbarrier == s->nworkers);

    for (int w = 0; w < s->nworkers; ++ w) {
        dlworker_destroy(s->workers + w);
    }
    s->nworkers = 0;

    if ((errno = dlwait_destroy(&s->stall))) {
        perror("dlsched_destroy freeing dlwait");
        exit(errno);
    }
}

int
dlsched_init(
    struct dlsched *s,
    int             nworkers,
    struct dltask  *task,
    dlwentryfn      entryfn,
    dlwexitfn       exitfn
)
{
    if (task == NULL) return errno = EINVAL;

    int result = 0;

    atomic_init(&s->terminate, 0);
    atomic_init(&s->wbarrier, nworkers);
    s->nworkers  = nworkers;

    result = dlwait_init(&s->stall);
    if (result) goto stall_init_failed;

    int w = 0;
    for (; w < nworkers; ++ w) {
        if (w == 0) {
            result = dlworker_init(s->workers + w, s, task, entryfn, exitfn, w);
        } else {
            result = dlworker_init(s->workers + w, s, NULL, entryfn, exitfn, w);
        }
        if (result) goto dlworker_init_failed;
    }

    return result;

dlworker_init_failed: {
        /* Attempt to destroy any threads that were created */
        for (int unwind = w; unwind > 0; -- unwind) {
            dlsched_terminate(s);
            dlworker_join(s->workers + (unwind-1));
            dlworker_destroy(s->workers + (unwind-1));
        }
    }
    if ((errno = dlwait_destroy(&s->stall))) {
        perror("dlsched_init::dlworker_init_failed freeing stall");
        exit(errno);
    }
stall_init_failed:
    return errno = result;
}

void
dlsched_join(struct dlsched *s)
{
    for (int w = 0; w < s->nworkers; ++ w) {
        dlworker_join(s->workers + w);
    }
}

/*
 * Literature dictates a random distribution of victims is more
 * performant than a linear search, but I just can't beat this
 * performance! Further testing required...
 */
int
dlsched_steal(struct dlsched *s, struct dltask **dst, int src)
{
    int tgt = 0;
    for (int n = 0; n < s->nworkers; ++ n) {
        if (tgt == src) {
            ++ tgt;
            continue;
        }
        struct dlworker *victim = s->workers + tgt;
        steal: ;
        int rc = dltqueue_steal(&victim->tqueue, dst);
        if (rc == EAGAIN) {
            _mm_pause();
            goto steal;
        }
        switch (rc) {
            case ENODATA: ++ tgt; break;
            case 0: return 0;
        }
    }
    return ENODATA;
}

void
dlsched_terminate(struct dlsched *s)
{
    atomic_store(&s->wbarrier, 0);
    atomic_store(&s->terminate, 1);

    int exited;
    do {
        if ((errno = dlwait_broadcast(&s->stall))) {
            perror("dlsched_terminate failed to broadcast on stall");
            exit(errno);
        }
        exited = atomic_load(&s->wbarrier);
        /* if this is called from a worker thread, consider it exited */
        if (dl_this_worker) ++ exited;
    } while (exited < s->nworkers);
}
