#ifndef DEADLOCK_TYPES_H_
#define DEADLOCK_TYPES_H_

#include <stdatomic.h>
#include <stddef.h>

struct dltask;

/*
 * Intel 64 and IA-32 references manuals instruct you to align memory to 128
 * bytes to make use of the L2 streamer, which will prefetch the line pair of
 * a block of cachelines.
 */
#define DEADLOCK_CLSZ 128

/*
 * Task functions are invoked and passed a pointer to the scheduled dltask
 * object.
 *
 * dldowncast() should be used to retrieve the outer task structure from
 * within a dltaskfn.
 */
typedef void(*dltaskfn)(struct dltask *);

/*
 * A dltask contains a function pointer to invoke and an optional next task to
 * conditionally invoke upon successful execution of this task. wait is the
 * number of tasks this task is waiting on before it will be scheduled.
 */
struct dltask {
	dltaskfn       fn;
	struct dltask *next;
	atomic_uint    wait;
};

/*
 * dlwentryfn, when not NULL, is invoked by every worker thread after
 * initializing thread local storage and before entering the task running loop.
 *
 * dlwexitfn, when not NULL, is invoked by every worker thread before exiting.
 */
typedef void (*dlwentryfn)(int worker_index);
typedef void (*dlwexitfn)(int worker_index);

/*
 * dldowncast() returns a pointer to the structure containing a dltask struct.
 * This should be used within a dltaskfn to retrieve the actual task object.
 *
 * I can't beat Wikipedia's ANSI implementation of Linux's container_of macro:
 * https://en.wikipedia.org/wiki/Offsetof
 */
#define dldowncast(ptr, T, memb) ((T *)((char *)(1 ? (ptr) : &((T *)0)->memb) - offsetof(T, memb)))

#endif /* DEADLOCK_TYPES_H_ */
