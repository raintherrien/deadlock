#ifndef DEADLOCK_INTERNAL_H_
#define DEADLOCK_INTERNAL_H_

#include <stdatomic.h>
#include <stddef.h>

struct dltask_;
typedef void(*dltaskfn)(struct dltask_ *);

/*
 * struct dltask_ should be considered an opaque type by client code and only
 * manipulated by the public deadlock API. This is primarily due to embedded
 * debug code when profiling is enabled, which DL_TASK_INIT() helps set up.
 *
 * Architecturally, we have a simple structure with a function to invoke,
 * passing this structure as its only argument, a possibly NULL pointer to
 * some other task object which is currently awaiting this and possibly more
 * tasks to complete before executing, and a wait counter which counts how
 * many tasks this task is waiting on to execute. With this simple bottom-up
 * dependency chain, where one task can wait on many parent tasks, but a task
 * can only block a single child task, we can construct a DAG of tasks.
 */
struct dltask_ {
	dltaskfn        fn_;
	struct dltask_ *next_;
	atomic_uint     wait_;

#ifdef DEADLOCK_PROFILE
	unsigned int    tid_;
#endif
};

#ifdef DEADLOCK_PROFILE
/* When profiling we assign a unique ID to each task upon initialization. */
extern atomic_uint dl_global_tid;
#define DL_TASK_INIT(fn) ((dltask){                                     \
		.fn_ = fn,                                              \
		.tid_ = atomic_fetch_add_explicit(&dl_global_tid, 1,    \
		                                  memory_order_relaxed) \
	})

#else
#define DL_TASK_INIT(fn) ((dltask){ .fn_ = fn })
#endif

#endif /* DEADLOCK_INTERNAL_H_ */
