#ifndef DEADLOCK_WORKER_H_
#define DEADLOCK_WORKER_H_

#include "tqueue.h"
#include <pthread.h>

/*
 * Each dlworker owns a queue of tasks and spawns a thread to execute
 * them. Work-stealing ensues and inevitably something seg-faults. :)
 *
 * dlworker_async() runs a task asynchronously. If this worker's task
 * queue is full the task is executed immediately. Some care should be
 * taken to avoid stack overflows. TODO: See source comments about
 * _mm_pause alternative, which would not suffer from potential stack
 * overflows at the cost of latency.
 *
 * dlworker_destroy() must be called to destroy an initialized worker.
 * Termination must be signalled on the scheduler and this worker must
 * be woken from any stall state, otherwise dlworker_destroy will spin
 * forever trying to join the worker's thread.
 *
 * dlworker_init() initializes a new worker.
 * Zero is returned on success, otherwise the worker is left
 * uninitialized and:
 * EAGAIN shall be returned if the system lacks the necessary resources
 * to spawn the worker thread.
 * ENOMEM shall be returned if insufficient memory exists to initialize
 * the worker.
 */

struct dlsched;

struct dlworker {
    struct dltqueue tqueue;
    struct dlsched *sched;
    dlwentryfn      entry;
    dlwexitfn       exit;
    pthread_t       thread;
    int             index;
};

void dlworker_async  (struct dlworker *, struct dltask *);
void dlworker_destroy(struct dlworker *);
void dlworker_join   (struct dlworker *);
int  dlworker_init(
    struct dlworker *w,
    struct dlsched  *s,
    struct dltask   *task,
    dlwentryfn       entry,
    dlwexitfn        exit,
    int              index
);

#endif /* DEADLOCK_WORKER_H_ */
