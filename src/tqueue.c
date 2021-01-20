#include "tqueue.h"
#include <errno.h>
#include <stdlib.h>

void
dltqueue_destroy(struct dltqueue *q)
{
    q->szmask = 0;
    free(q->tasks);
    q->tasks = NULL;
    atomic_store(&q->head, 0);
    atomic_store(&q->tail, 0);
}

int
dltqueue_init(struct dltqueue *q, unsigned int size)
{
    if (size < 2 || (size & (size - 1)) != 0) {
        return errno = EINVAL;
    }

    q->tasks = malloc(size * sizeof *q->tasks);
    if (!q->tasks) {
        return errno;
    }
    for (unsigned i = 0; i < size; ++ i) {
        atomic_store(&q->tasks[i], NULL);
    }
    q->szmask = size - 1;
    atomic_store(&q->head, 0);
    atomic_store(&q->tail, 0);
    return 0;
}

int
dltqueue_push(struct dltqueue *q, struct dltask *tsk)
{
    unsigned h = atomic_load_explicit(&q->head, memory_order_relaxed);
    unsigned t = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (h - t > q->szmask) {
        /* No free space */
        return ENOBUFS;
    }
    atomic_store_explicit(&q->tasks[h & q->szmask], tsk, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&q->head, h + 1, memory_order_relaxed);
    return 0;
}

int
dltqueue_steal(struct dltqueue *q, struct dltask **dst)
{
    unsigned t = atomic_load_explicit(&q->tail, memory_order_acquire);
    atomic_thread_fence(memory_order_seq_cst);
    unsigned h = atomic_load_explicit(&q->head, memory_order_acquire);

    if (h <= t) {
        return ENODATA; /* Empty */
    }

    *dst = atomic_load_explicit(&q->tasks[t & q->szmask],
                                memory_order_relaxed);
    if (!atomic_compare_exchange_strong_explicit(
          &q->tail, &t, t + 1,
          memory_order_seq_cst, memory_order_relaxed))
    {
        /* Failed race */
        *dst = NULL;
        return EAGAIN;
    }
    return 0;
}

/*
 * I believe the implementation of take in Correct and efficient work-
 * stealing for weak memory models is bugged. If we're dealing with
 * unsigned indices and the queue is empty, bottom - 1 could potentially
 * wrap around to SIZE_MAX which would be larger than tail. This doesn't
 * matter if we're just fetching and returning a NULL pointer from our
 * buffer, but it does matter when we want an accurate error code
 * returned!
 * A solution is to perform an early test for emptiness.
 *
 * TODO: It would be interesting to see how other people have
 * implemented this, I'm probably wrong!
 */
int
dltqueue_take(struct dltqueue *q, struct dltask **dst)
{
    int rc = 0;

    /* Check for empty. This is not in the source paper */
    unsigned h = atomic_load_explicit(&q->head, memory_order_relaxed);
    unsigned t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    if (t >= h) return ENODATA;

    /* Continue with source impl of take */
    h -= 1;
    atomic_store_explicit(&q->head, h, memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);
    t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    if (t <= h) {
        /* Not empty */
        *dst = atomic_load_explicit(&q->tasks[h & q->szmask], memory_order_relaxed);
        if (t == h) {
            /* Last task */
            if (!atomic_compare_exchange_strong_explicit(
                  &q->tail, &t, t + 1,
                  memory_order_seq_cst, memory_order_relaxed))
            {
                /* Failed race */
                *dst = NULL;
                rc = EAGAIN;
            }
            atomic_store_explicit(&q->head, h + 1, memory_order_relaxed);
        }
    } else {
        /* Empty */
        *dst = NULL;
        rc = ENODATA;
        atomic_store_explicit(&q->head, h + 1, memory_order_relaxed);
    }
    return rc;
}
