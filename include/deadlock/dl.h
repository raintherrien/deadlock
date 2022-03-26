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
 * dlcreate() returns a new ready-to-run task. This task will not be invoked
 * until released by dldetach(). Any tasks that must execute before this task
 * must be created with dlcreate(), passing this task as the next pointer.
 * This must be done before calling dldetach().
 *
 * dldetach() releases a task created by dlcreate() and marks it executable.
 * This must be called exactly once for every task created or recaptured.
 * This must be called *after* creating any tasks which must execute before
 * this task, passing this task as the next pointer.
 *
 * dlrecapture() must be passed the currently executing task. This task is
 * reset as if it were just created, with a new body function, but retains
 * the same next pointer it was created with. This task must be released by
 * calling dldetach() just like a newly created task. Before detaching, this
 * task may be passed as the next pointer to dlcreate() to create tasks which
 * must be executed before this task is re-executed.
 *
 * dlrecapture() may be used to create a subgraph which forks its own children
 * using dlcreate() that join back to this task, by passing the recaptured
 * task as the next pointer. By doing this you can emulate SBRM by having a
 * "create" and "destroy" function execute on the same task object and
 * suspend any dependent tasks set by the caller until any amount of work is
 * completed and the continuation is complete.
 */
dltask dlcreate(dltaskfn fn, dltask *next);
void   dldetach(dltask *task);
void   dlrecapture(dltask *current_task, dltaskfn continuaton_fn);

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
 * DL_TASK_ENTRY_VOID performs the same static initialization of DL_TASK_ENTRY
 * but without deriving any task context structure.
 */
#if 0
#define DL_TASK_ENTRY_VOID
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
