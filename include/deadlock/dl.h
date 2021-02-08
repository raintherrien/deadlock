#ifndef DEADLOCK_DL_H_
#define DEADLOCK_DL_H_

#include "deadlock/internal.h"

/*
 * Intel 64 and IA-32 references manuals instruct you to align memory to 128
 * bytes to make use of the L2 streamer, which will prefetch the line pair of
 * a block of cachelines.
 */
#define DEADLOCK_CLSZ 128

/*
 * dltask should be treated as an opaque type by client code and only
 * manipulated by this public API. See internal.h for further explanation.
 */
typedef struct dltask_ dltask;

/*
 * Each dltask must be assigned a function to invoke upon execution. This
 * function is passed the dltask object, from which the outer task object may
 * be retrieved by DL_TASK_DOWNCAST().
 * TODO: This is done automatically by DL_TASK_ENTRY()
 */
typedef void(*dltaskfn)(dltask *);

/*
 * dlwentryfn and dlwexitfn are optional pointers passed to a scheduler upon
 * initialization, to be invoked during a thread worker's lifetime, upon entry
 * and exit. These are useful for setting up and dumping thread local state.
 */
typedef void (*dlwentryfn)(int worker_index);
typedef void (*dlwexitfn) (int worker_index);

/*
 * DL_TASK_DOWNCAST() returns a pointer to the structure containing a dltask.
 * This should be used within a dltaskfn to retrieve the actual task object.
 *
 * For example the following downcasts a dltask pointer to its container:
 * 	struct container_pkg {
 * 		...
 * 		dltask *dlt;
 * 		...
 * 	};
 * 	void container_run(dltask *xdlt) {
 * 		struct container_pkg *pkg;
 * 		pkg = DL_TASK_DOWNCAST(xdlt, struct container_pkg, xdlt);
 * 		...
 * 	}
 *
 * I can't beat Wikipedia's ANSI implementation of Linux's container_of macro:
 * https://en.wikipedia.org/wiki/Offsetof
 */
#define DL_TASK_DOWNCAST(ptr, T, memb) \
	((T *)((char *)(1 ? (ptr) : &((T *)0)->memb) - offsetof(T, memb)))

/*
 * DL_TASK_INIT is a function-like macro which evaluates to a dltask. This
 * macro performs additional bookkeeping is profiling is enabled.
 */
#if 0
#define DL_TASK_INIT(fn)
#endif

/*
 * dlasync() schedules a task to execute on the current task scheduler. Must
 * be called from a worker thread because it identifies the current task
 * scheduler from a thread local superblock. next is an optional task which
 * depends on this task's completion before executing.
 *
 * dlcontinuation() should be passed the current task and marks it as
 * incomplete when this invocation completes. Instead the function associated
 * with this task is updated and any task depending on this task's completion
 * is not invoked until this task is invoked next.
 * Typically dlcontinuation is used by a subgraph which forks its own children
 * using dlasync that join back to this task using dldefer. By doing this you
 * can emulate SBRM by having a "create" and "destroy" function execute on the
 * same task object and suspend any dependent tasks set by the caller until
 * any amount of work is completed and the continuation is complete.
 *
 * dldefer() increments the number of dependencies on a task. This should be
 * equal to the number of dlasync invocations this task is passed to.
 *
 * dlmain() and dlmainex() initialize the default task scheduler, passing a
 * root task to execute, and block until termination is signalled.
 * Zero is returned on success, otherwise errno is set and returned.
 * Either way the default scheduler is left uninitialized once this function
 * returns.
 *
 * dlterminate() signals the current task scheduler to terminate. Like
 * dlasync() this must be called from a worker thread.
 *
 * dlworker_index() returns the index of this worker thread.
 */

void   dlasync       (dltask *task, dltask *next);
void   dlcontinuation(dltask *task, dltaskfn continuefn);
void   dldefer       (dltask *task, unsigned count);
int    dlmain        (dltask *, dlwentryfn, dlwexitfn);
int    dlmainex      (dltask *, dlwentryfn, dlwexitfn, int workers);
void   dlterminate   (void);
int    dlworker_index(void);

#endif /* DEADLOCK_DL_H_ */
