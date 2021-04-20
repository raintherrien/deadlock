#ifndef DEADLOCK_DL_H_
#define DEADLOCK_DL_H_

/*
 * dltask should be treated as an opaque type by client code and only
 * manipulated by this public API. See internal.h for further explanation.
 */
typedef struct dltask_ dltask;

/*
 * DL_TASK_ARGS should be used to declare and define dltaskfn functions. This
 * really does nothing except obscure the argument list and give the dltask
 * arg a consistent name for DL_TASK_ENTRY to work with, which is a much more
 * important macro.
 */
#define DL_TASK_ARGS void *dlw_param, dltask *dlt_param

/*
 * Each dltask must be assigned a function to invoke upon execution. This
 * function is passed the dltask object, from which the outer task object may
 * be retrieved by DL_TASK_DOWNCAST().
 */
typedef void(*dltaskfn)(DL_TASK_ARGS);

/*
 * dlasync() schedules a task to execute on the current task scheduler. Must
 * be called from a worker thread because it identifies the current task
 * scheduler from a thread local superblock.
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
 * dlnext() creates a dependency between a task and its child. This function
 * must be called in concert with dlwait() to increment the wait counter of
 * the child task. dlnext() can be called to associate a task with multiple
 * parents but a task can have only one child.
 *
 * dlswap() Queues the passed task "other" for invocation immediately. Any
 * tasks waiting on "this" task are now also waiting on "other". This is an
 * alternative to dlcontinuation() "block based" recursion. This is like a
 * tail call to a different task.
 *
 * dltail() recursively invokes a task. The task passed should be the task
 * currently executing, and dltail() should only be called before immediately
 * ending the current task invocation. Conceptually, the task is re-invoked
 * before dltail() returns and thus the current task will be running at the
 * same time or might even finish after the spawned task.
 *
 * dlwait() increments the number of dependencies on a task. This should be
 * equal to the number of dlasync invocations this task is passed to.
 */
void dlasync(dltask *task);
void dlcontinuation(dltask *task, dltaskfn continuefn);
void dlnext(dltask *task, dltask *next);
void dlswap(dltask *this, dltask *other);
void dltail(dltask *task);
void dlwait(dltask *task, unsigned wait);

/*
 * DL_TASK_INIT is a function-like macro which returns an initialized dltask.
 * A task is in an undefined state unless initialized with DL_TASK_INIT.
 */
#if 0
#define DL_TASK_INIT(fn)
#endif

/*
 * DL_TASK_ENTRY downcasts the dltask arg to a typed structure and performs
 * static initialization of this task, registering it globally and storing
 * info such as file, line, function name, as well as dynamic registration of
 * this particular invocation (node in a task graph).
 *
 * See DL_TASK_ARGS for usage.
 */
#if 0
#define DL_TASK_ENTRY(outer_type, var, memb)
#endif

/*
 * dlwentryfn and dlwexitfn are optional pointers passed to a scheduler upon
 * initialization, to be invoked during a thread worker's lifetime, upon entry
 * and exit. These are useful for setting up and dumping thread local state.
 */
typedef void (*dlwentryfn)(int worker_index);
typedef void (*dlwexitfn) (int worker_index);


/*
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
int dlmain(dltask *, dlwentryfn, dlwexitfn);
int dlmainex(dltask *, dlwentryfn, dlwexitfn, int workers);
void dlterminate(void);
int dlworker_index(void);

/*
 * DL_TASK_DOWNCAST() returns a pointer to the structure containing a dltask.
 * This should be used within a dltaskfn to retrieve the actual task object.
 * You probably don't need this and should use DL_TASK_ENTRY() instead.
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
	((void *)((char *)(1 ? (ptr) : &((T *)0)->memb) - offsetof(T, memb)))

/*
 * Intel 64 and IA-32 references manuals instruct you to align memory to 128
 * bytes to make use of the L2 streamer, which will prefetch the line pair of
 * a block of cachelines.
 */
#define DEADLOCK_CLSZ 128

#include "deadlock/internal.h"

#endif /* DEADLOCK_DL_H_ */
