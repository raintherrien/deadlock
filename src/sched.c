#include "sched.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <immintrin.h> /* _mm_pause */

void *
dlsched_alloc(int nworkers)
{
    /* Workers in dlsched are flexible array member */
    return malloc(sizeof(struct dlsched) +
                  sizeof(struct dlworker) * nworkers);
}

void
dlsched_destroy(struct dlsched *s)
{
    assert(s->wbarrier == s->nworkers);

    for (int w = 0; w < s->nworkers; ++ w) {
        dlworker_destroy(s->workers + w);
    }
    s->nworkers = 0;

    if ((errno = pthread_cond_destroy (&s->stallcv )) ||
        (errno = pthread_mutex_destroy(&s->stallmtx))) {
        perror("dlsched_destroy freeing pthread objects: ");
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

    s->terminate = 0;
    s->wbarrier  = nworkers;
    s->nworkers  = nworkers;

    result = pthread_cond_init(&s->stallcv, NULL);
    if (result) goto stallcv_init_failed;

    result = pthread_mutex_init(&s->stallmtx, NULL);
    if (result) goto stallmtx_init_failed;

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
    if (errno = pthread_mutex_destroy(&s->stallmtx)) {
        perror("dlsched_init::dlworker_init_failed freeing stallmtx: ");
        exit(errno);
    }
stallmtx_init_failed:
    if (errno = pthread_cond_destroy(&s->stallcv)) {
        perror("dlsched_init::stallmtx_init_failed freeing stallcv: ");
        exit(errno);
    }
stallcv_init_failed:
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
dlsched_steal(struct dlsched *s, struct dltask **dst, size_t src)
{
    size_t tgt = 0;
    for (int n = 0; n < s->nworkers; ++ n) {
        if (tgt == src) {
            ++ tgt;
            continue;
        }
        struct dlworker *victim = s->workers + tgt;
        int rc;
        do {
            rc = dltqueue_steal(&victim->tqueue, dst);
            _mm_pause();
        } while (rc == EAGAIN);
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
    atomic_store(&s->wbarrier, s->nworkers);
    atomic_store(&s->terminate, 1);

    do {
        if ((errno = pthread_cond_broadcast(&s->stallcv))) {
            perror("dlsched_terminate failed to broadcast on stallcv: ");
            exit(errno);
        }
    } while (atomic_load(&s->wbarrier) < s->nworkers);
}
