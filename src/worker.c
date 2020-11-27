#include "worker.h"
#include "sched.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

#include <sched.h> /* sched_yield */
#include <immintrin.h> /* _mm_pause */

_Thread_local struct dlworker *dl_this_worker;

/*
 * dlworker_entry() is the main loop of each worker thread. It spins
 * until the scheduler signals termination, popping work from the local
 * queue and attempting to steal from other work queues when the local
 * work dries up.
 *
 * dlworker_invoke() invokes a task and queues next if one exists.
 *
 * dlworker_stall() blocks until more tasks are queued.
 *
 * dlworker_work() finds a task and executes it.
 * Zero is returned on success, otherwise no work is done and:
 * ENODATA is returned if the worker is unable to dequeue or steal work.
 */
static void *dlworker_entry (void *);
static void  dlworker_invoke(struct dlworker *, struct dltask *);
static void  dlworker_stall (struct dlworker *);
static int   dlworker_work  (struct dlworker *);

void
dlworker_async(struct dlworker *w, struct dltask *t)
{
    /*
     * dltqueue_push shall only return success or ENOBUFS. If there is
     * no space, execute this task immediately. Alternatively we could
     * _mm_pause(). TODO: Benchmark
     */
    switch (dltqueue_push(&w->tqueue, t)) {
    case 0: {
        int result = pthread_cond_signal(&w->sched->stallcv);
        if (result) {
            errno = result;
            perror("dlworker_async failed to signal stallcv: ");
            exit(errno);
        }
        break;
    }
    case ENOBUFS:
        dlworker_invoke(w, t);
        break;
    }
}

void
dlworker_destroy(struct dlworker *w)
{
    dltqueue_destroy(&w->tqueue);
}

void
dlworker_join(struct dlworker *w)
{
    if ((errno = pthread_join(w->thread, NULL))) {
        perror("dlworker_join failed to join worker thread: ");
        exit(errno);
    }
}

int
dlworker_init(
    struct dlworker *w,
    struct dlsched  *s,
    struct dltask   *task,
    dlwentryfn       entry,
    dlwexitfn        exit,
    int              index
)
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
    result = pthread_create(&w->thread, NULL, &dlworker_entry, w);
    if (result) goto pthread_create_failed;

    return 0;

pthread_create_failed:
tqueue_prime_failed:
    dltqueue_destroy(&w->tqueue);
tqueue_init_failed:
    return errno = result;
}

static void *
dlworker_entry(void *xworker)
{
    struct dlworker *w = xworker;

    /* Global thread local pointer to this worker used by async etc. */
    dl_this_worker = w;

    /* Invoke the on_entry lifetime callback */
    if (w->entry) w->entry(w->index);

    /* Synchronize all workers before they start potentially stealing */
    atomic_fetch_sub(&w->sched->wbarrier, 1);
    while(atomic_load(&w->sched->wbarrier) > 0)
    {
        /* Short-circuit if terminate is called, not calling exit */
        if (atomic_load(&w->sched->terminate)) {
            return NULL;
        }
        sched_yield();
    }

    /* Work loop */
    size_t stall_count = 0;
    while (!atomic_load_explicit(&w->sched->terminate,
              memory_order_relaxed))
    {
        if (dlworker_work(w) != ENODATA) {
            stall_count = 0;
        } else {
            if (++ stall_count < 16) {
                dlworker_stall(w);
                stall_count = 0;
            } else {
                sched_yield();
            }
        }
    }

    /* Invoke the on_exit lifetime callback */
    if (w->exit) w->exit(w->index);

    /* Clean up thread local state */
    dl_this_worker = NULL;
    /* Synchronize workers until they're all joinable */
    atomic_fetch_add(&w->sched->wbarrier, 1);

    return NULL;
}

static void
dlworker_invoke(struct dlworker *w, struct dltask *t)
{
    struct dlnext *next = t->next;

    (*t->fn)(t);

    if (next != NULL && 1 == atomic_fetch_sub_explicit(&next->wait, 1,
                               memory_order_release))
    {
        dlworker_async(w, &next->task);
    }
}

static void
dlworker_stall(struct dlworker *w)
{
    int pr;
    if ((pr = pthread_mutex_lock(&w->sched->stallmtx)) ||
        (pr = pthread_cond_wait(&w->sched->stallcv, &w->sched->stallmtx)) ||
        (pr = pthread_mutex_unlock(&w->sched->stallmtx)))
    {
        errno = pr;
        perror("dlworker_stall failed to wait on cv: ");
        exit(errno);
    }
}

static int
dlworker_work(struct dlworker *w)
{
    struct dltask *t = NULL;

    int rc;
    do {
        rc = dltqueue_take(&w->tqueue, &t);
        _mm_pause();
    } while (rc == EAGAIN);
    switch (rc) {
    case 0:       goto invoke;
    case ENODATA: goto steal;
    }

steal:
    if (dlsched_steal(w->sched, &t, w->index)
          == ENODATA)
    {
        return ENODATA;
    }

invoke:
    dlworker_invoke(w, t);
    return 0;
}
