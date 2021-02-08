#ifndef DEADLOCK_ASYNC_H_
#define DEADLOCK_ASYNC_H_

#include "types.h"

/*
 * dlasync() schedules a task to execute on the current task scheduler. Must
 * be called from a worker thread because it identifies the current task
 * scheduler from a thread local superblock.
 *
 * dlcontinuation() marks next as a continuation of this task, effectively
 * stalling any tasks waiting on this task from invoking until next also
 * completes. TODO: example
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

void dlasync       (struct dltask *);
void dlcontinuation(struct dltask *this, struct dltask *next);
int  dlmain        (struct dltask *, dlwentryfn, dlwexitfn);
int  dlmainex      (struct dltask *, dlwentryfn, dlwexitfn, int workers);
void dlterminate   (void);
int  dlworker_index(void);

#endif /* DEADLOCK_ASYNC_H_ */
