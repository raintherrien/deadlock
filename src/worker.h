#ifndef DEADLOCK_WORKER_H_
#define DEADLOCK_WORKER_H_

#include "deadlock/graph.h"

#include "thread.h"
#include "tqueue.h"

/*
 * Each dlworker owns a queue of tasks and spawns a thread to execute them.
 * Work-stealing ensues and inevitably something seg-faults. :)
 *
 * dlworker_async() runs a task asynchronously. If this worker's task queue is
 * full the task is executed immediately. Care should be taken to avoid stack
 * overflows and livelocks, namely: assume dlworker_async will always invoke
 * the task immediately on the stack.
 *
 * dlworker_destroy() must be called to destroy an initialized worker.
 * Termination must be signalled on the scheduler and this worker must be
 * woken from any stall state, otherwise dlworker_destroy will spin forever
 * trying to join the worker's thread.
 *
 * dlworker_init() initializes a new worker. Zero is returned on success
 * otherwise the worker is left uninitialized and either:
 * EAGAIN shall be returned if the system lacks the necessary resources to
 * spawn the worker thread, or
 * ENOMEM shall be returned if insufficient memory exists to initialize the
 * worker.
 */

struct dlsched;

struct dlworker {
	struct dltqueue  tqueue;
	struct dlsched  *sched;
	struct dlthread  thread;
	dlwentryfn       entry;
	dlwexitfn        exit;
	int              index;

	/*
	 * When graphing it's useful to store information about the currently
	 * executing task in this threads worker struct. This eliminates
	 * needless argument passing.
	 */
#if DEADLOCK_GRAPH_EXPORT
	struct dlgraph_node current_node;
	struct dlgraph *current_graph;
	unsigned long   invoked_task_id;
#endif
};

void dlworker_async  (struct dlworker *, dltask *);
void dlworker_destroy(struct dlworker *);
void dlworker_join   (struct dlworker *);
int  dlworker_init   (struct dlworker *, struct dlsched *, dltask *,
                      dlwentryfn, dlwexitfn, int index);

/*
 * dl_this_worker is defined in worker.c and is the thread local superblock of
 * information about this thread's worker.
 */
extern _Thread_local struct dlworker *dl_this_worker;

#endif /* DEADLOCK_WORKER_H_ */
