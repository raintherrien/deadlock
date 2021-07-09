#ifndef DEADLOCK_THREAD_H_
#define DEADLOCK_THREAD_H_

struct dlthread;
struct dlwait;

typedef void(*dlthreadfn)(void *);

static int  dlprocessorcount(void);
static int  dlthread_create(struct dlthread *, dlthreadfn, void *, int);
static int  dlthread_join(struct dlthread *);
static void dlthread_yield(void);
static int  dlwait_broadcast(struct dlwait *);
static int  dlwait_wait(struct dlwait *);
static int  dlwait_init(struct dlwait *);
static int  dlwait_destroy(struct dlwait *);

#if defined(_WIN32)

#include <windows.h>

static inline int
dlprocessorcount(void)
{
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	return (int)sysinfo.dwNumberOfProcessors;
}

#else

/* TODO: Wrap in CMake macro? */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <unistd.h> /* sysconf */

static inline int
dlprocessorcount(void)
{
	long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
	if (ncpu < 0) return errno;
	if (ncpu == 0 || ncpu > INT_MAX) return errno = ERANGE;
	return (int)ncpu;
}

#endif

/* Assume mingw32/64 uses POSIX threads */
#if defined(_WIN32) && !defined(__MINGW32__)

struct dlthread {
	dlthreadfn fn;
	void      *arg;
	HANDLE     handle;
};

struct dlwait {
	CONDITION_VARIABLE cv;
	SRWLOCK srwlock;
	int should_wait;
};

static inline DWORD
dlwinthreadfwd(LPVOID xdlt)
{
	struct dlthread *dlt = xdlt;
	dlt->fn(dlt->arg);
	return 1;
}

static inline int
dlthread_create(struct dlthread *t, dlthreadfn fn, void *arg, int affinity)
{
	t->fn = fn;
	t->arg = arg;
	t->handle = CreateThread(NULL, 0, dlwinthreadfwd, t, 0, NULL);
	if (t->handle == NULL) {
		/* TODO: GetLastError does not return errno values */
		return -1;
	}
	if (affinity < 0 ||
            affinity >= dlprocessorcount() ||
	    SetThreadAffinityMask(t->handle, 1 << affinity) == 0)
	{
		/* TODO: Warning? */
	}
	return 0;
}

static inline int
dlthread_join(struct dlthread *t)
{
	DWORD waitrc = WaitForSingleObject(t->handle, INFINITE);
	BOOL closed = CloseHandle(t->handle);
	if (!closed) {
		/* TODO: GetLastError does not return errno values */
		return -1;
	}
	if (waitrc != WAIT_OBJECT_0) {
		/* TODO: GetLastError does not return errno values */
		return -1;
	}
	return 0;
}

static inline void
dlthread_yield(void)
{
	/* Cannot fail, may do nothing :) */
	YieldProcessor();
}

static inline int
dlwait_broadcast(struct dlwait *wait)
{
	/* Cannot fail */
	AcquireSRWLockShared(&wait->srwlock);
	wait->should_wait = 0;
	ReleaseSRWLockShared(&wait->srwlock);
	WakeConditionVariable(&wait->cv);
	return 0;
}

static inline int
dlwait_wait(struct dlwait *wait)
{
	BOOL success = TRUE;
	AcquireSRWLockShared(&wait->srwlock);
	while (wait->should_wait && (success ||
	                             GetLastError() == ERROR_TIMEOUT))
	{
		success = SleepConditionVariableSRW(
		            &wait->cv, &wait->srwlock,
		            INFINITE,
		            CONDITION_VARIABLE_LOCKMODE_SHARED);
	}
	wait->should_wait = 1;
	ReleaseSRWLockShared(&wait->srwlock);
	if (success || GetLastError() == ERROR_TIMEOUT) return 0;
	else {
		/* TODO: GetLastError does not return errno values */
		return -1;
	}
}

static inline int
dlwait_init(struct dlwait *wait)
{
	/* Neither ops can fail */
	InitializeSRWLock(&wait->srwlock);
	InitializeConditionVariable(&wait->cv);
	wait->should_wait = 1;
	return 0;
}

static inline int
dlwait_destroy(struct dlwait *wait)
{
	/* Win32 SRW and CV are stateless :) */
	(void) wait;
	return 0;
}

#else

#include <pthread.h>
#include <sched.h>  /* sched_yield */

struct dlthread {
	dlthreadfn fn;
	void      *arg;
	pthread_t  handle;
};

struct dlwait {
	pthread_cond_t cv;
	pthread_mutex_t mtx;
	int should_wait;
};

static inline void *
dlpthreadfwd(void *arg)
{
	struct dlthread *dlt = arg;
	dlt->fn(dlt->arg);
	return NULL;
}

static inline int
dlthread_create(struct dlthread *t, dlthreadfn fn, void *arg, int affinity)
{
	t->fn = fn;
	t->arg = arg;
	int rc = pthread_create(&t->handle, NULL, dlpthreadfwd, t);
	if (rc)
		return rc;

	if (affinity < 0 || affinity >= dlprocessorcount()) {
		/* TODO: Warn? */
	} else {
		cpu_set_t cpuset;
		CPU_ZERO(&cpuset);
		CPU_SET(affinity, &cpuset);
		(void) pthread_setaffinity_np(t->handle, sizeof(cpu_set_t), &cpuset);
	}
	return 0;
}

static inline int
dlthread_join(struct dlthread *t)
{
	return pthread_join(t->handle, NULL);
}

static inline void
dlthread_yield(void)
{
	sched_yield();
}

static inline int
dlwait_broadcast(struct dlwait *wait)
{
	int pr;
	if ((pr = pthread_mutex_lock(&wait->mtx)))
		return pr;
	wait->should_wait = 0;
	if ((pr = pthread_mutex_unlock(&wait->mtx)))
		return pr;
	return pthread_cond_signal(&wait->cv);
}

static inline int
dlwait_wait(struct dlwait *wait)
{
	int pr;

	if ((pr = pthread_mutex_lock(&wait->mtx)))
		return pr;

	while (wait->should_wait) {
		if ((pr = pthread_cond_wait(&wait->cv, &wait->mtx)))
			return pr;
	}
	wait->should_wait = 1;
	if ((pr = pthread_mutex_unlock(&wait->mtx)))
		return pr;

	return 0;
}

static inline int
dlwait_init(struct dlwait *wait)
{
	int pr;
	if ((pr = pthread_cond_init(&wait->cv, NULL)) ||
	    (pr = pthread_mutex_init(&wait->mtx, NULL)))
	{
		return pr;
	}
	wait->should_wait = 1;
	return 0;
}

static inline int
dlwait_destroy(struct dlwait *wait)
{
	int pr;
	if ((pr = pthread_cond_destroy(&wait->cv)) ||
	    (pr = pthread_mutex_destroy(&wait->mtx)))
	{
		return pr;
	}
	return 0;
}

#endif

#endif /* DEADLOCK_THREAD_H_ */
