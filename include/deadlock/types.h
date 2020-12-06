#ifndef DEADLOCK_TYPES_H_
#define DEADLOCK_TYPES_H_

#include <stdatomic.h>
#include <stddef.h>

struct dltask_;
typedef struct dltask_ dltask;

/*
 * Intel 64 and IA-32 references manuals instruct you to align memory to
 * 128 bytes to make use of the L2 streamer, which will prefetch the
 * line pair (second when aligned to 128B?) of a block of cachelines.
 */
#define DEADLOCK_CLSZ    64
//#define DEADLOCK_CLBLKSZ (2*DEADLOCK_CLBLKSZ)

/*
 * Task functions are invoked and passed a pointer to the scheduled task
 * instance.
 *
 * It is convention to make dltask the first member of your specialized
 * task struct A so that a pointer to A may be cast to a dltask pointer
 * to the first member (C11 6.7.2.1) and vice-versa. This is akin to
 * basic inheritance in C++.
 */
typedef void(*dltaskfn)(dltask *);

/*
 * A dltask contains a function pointer to invoke and an optional next
 * task to conditionally invoke upon successful execution of this task.
 * wait is the number of tasks this task is waiting on before it will be
 * scheduled.
 */
struct dltask_ {
    dltaskfn    fn;
    dltask     *next;
    atomic_uint wait;
};

/*
 * dlwentryfn, when not NULL, is invoked by every worker thread after
 * initializing thread local storage and before entering the task
 * running loop.
 *
 * dlwexitfn, when not NULL, is invoked by every worker thread before
 * exiting.
 */
typedef void (*dlwentryfn)(int worker_index);
typedef void (*dlwexitfn)(int worker_index);

#endif /* DEADLOCK_TYPES_H_ */
