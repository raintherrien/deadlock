#ifndef DEADLOCK_TQUEUE_H_
#define DEADLOCK_TQUEUE_H_

#include "deadlock/dl.h"
#include <stdatomic.h>

/*
 * Correct and efficient work-stealing for weak memory models.
 * Nhat Minh Lê, Antoniu Pop, Albert Cohen, and Francesco Zappa Nardelli
 * 2013. In Proceedings of the 18th ACM SIGPLAN symposium on Principles and
 * practice of parallel programming (PPoPP ’13).
 * Association for Computing Machinery, New York, NY, USA, 69–80.
 *
 * dltqueue_destroy() must be called to destroy an initialized queue.
 *
 * dltqueue_init() initializes a new fixed size queue.
 * Zero is returned on success, otherwise dltqueue is uninitialized and:
 * EINVAL shall be returned if size is not a power of two;
 * ENOMEM shall be returned if insufficient memory exists to initialize
 * the ring buffer.
 *
 * dltqueue_push() appends a task to the bottom of the queue.
 * Zero is returned on success, otherwise the task is not queued and:
 * ENOBUFS shall be returned if the queue is full.
 *
 * dltqueue_steal() moves the oldest task into dst.
 * Zero is returned on success, otherwise dst is undefined and:
 * ENODATA shall be returned if the queue is empty;
 * EAGAIN shall be returned if this thead failed to atomically acquire a task.
 *
 * dltqueue_take() movest the newest task into dst.
 * Zero is returned on success, otherwise dst is undefined and:
 * ENODATA shall be returned if the queue is empty;
 * EAGAIN shall be returned if this thead failed to atomically acquire a task.
 *
 * push, take, and steal cannot fail except with EAGAIN, ENOBUFS, and ENODATA
 * where specified above.
 */

typedef _Atomic(dltask *) atomic_task_ptr;

struct dltqueue {
	_Alignas(DEADLOCK_CLSZ)
	atomic_uint head;

	_Alignas(DEADLOCK_CLSZ)
	atomic_uint tail;

	atomic_task_ptr *tasks;
	unsigned int szmask;
};

void dltqueue_destroy(struct dltqueue *);
int  dltqueue_init   (struct dltqueue *, unsigned int size);
int  dltqueue_push   (struct dltqueue *, dltask *);
int  dltqueue_steal  (struct dltqueue *, dltask **dst);
int  dltqueue_take   (struct dltqueue *, dltask **dst);

#endif /* DEADLOCK_TQUEUE_H_ */
