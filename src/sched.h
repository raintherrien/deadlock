#ifndef DEADLOCK_SCHED_H_
#define DEADLOCK_SCHED_H_

#include "thread.h"
#include "worker.h"
#include <stdatomic.h>

/*
 * dlsched owns the lifetime of worker threads and facilitates worker
 * synchronization, work stealing, work starvation, and termination signal.
 *
 * dlsched_alloc() allocates a new scheduler with the provided number of
 * workers. This is a simple function wrapping malloc but useful because
 * workers are a flexible array member of dlsched. The returned pointer must
 * be freed by free() just like any other memory allocated by malloc.
 * An allocated but uninitialized dlsched is returned on success, otherwise
 * NULL is returned and:
 * ENOMEM shall be returned if insufficient memory exists to allocate
 * the scheduler.
 * TODO: In retrospect I would prefer a DLSCHED_SIZE macro and to let the user
 * provide a buffer.
 *
 * dlsched_destroy() must be called to destroy an initialized scheduler.
 *
 * dlsched_init() initializes a scheduler. A task is required to prime the
 * scheduler with work since there is no global work queue. Zero is returned
 * on success, otherwise the scheduler is uninitialized and errno is set and
 * returned.
 *
 * dlsched_join() blocks the calling thread until the scheduler is terminated.
 *
 * dlsched_steal() attempts to steal a task from all workers other than src
 * (the calling worker's index), starting with tgt. Zero is returned on
 * success otherwise ENODATA is returned if there are no available tasks.
 *
 * dlsched_terminate() signals a scheduler to terminate. All workers should
 * enter a joinable state. Calling sched_terminate before all workers are
 * initialized and in a running state is undefined and the application will
 * probably hang.
 */

struct dlsched {
	struct dlwait   stall;
	atomic_int      terminate;
	atomic_int      wbarrier;
	int             nworkers;
	struct dlworker workers[];
};

void *dlsched_alloc    (int nworkers);
void  dlsched_destroy  (struct dlsched *);
int   dlsched_init     (struct dlsched *, int nworkers, struct dltask *,
                        dlwentryfn, dlwexitfn);
void  dlsched_join     (struct dlsched *);
int   dlsched_steal    (struct dlsched *, struct dltask **, int src);
void  dlsched_terminate(struct dlsched *);

#endif /* DEADLOCK_SCHED_H_ */
