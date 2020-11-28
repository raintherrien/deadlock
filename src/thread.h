#ifndef DEADLOCK_THREAD_H_
#define DEADLOCK_THREAD_H_

/* XXX None of this file does error handling */

#ifdef _WIN32
#include <Windows.h>
#else
#include <pthread.h>
#include <sched.h> /* sched_yield */
#endif

struct dlthread {
#ifdef _WIN32
    HANDLE handle;
#else
    pthread_t handle;
#endif
};

struct dlwait {
#ifdef _WIN32
    CONDITION_VARIABLE cv;
    CRITICAL_SECTION   cs;
#else
    pthread_cond_t     cv;
    pthread_mutex_t    mtx;
#endif
};

#ifdef _WIN32

#define dlthread_create(THREAD, ENTRY, ARG) (!((THREAD)->handle = CreateThread(NULL, 0, ENTRY, ARG, 0, NULL)))
#define dlthread_join(THREAD) !(WaitForSingleObject((THREAD)->handle, INFINITE) == 0 && CloseHandle((THREAD)->handle) == 1)
#define dlsched_yield() YieldProcessor()

static inline int dlwait_signal(struct dlwait *wait)
{
    WakeConditionVariable(&wait->cv);
    return 0;
}

static inline int dlwait_broadcast(struct dlwait *wait)
{
    WakeAllConditionVariable(&wait->cv);
    return 0;
}

static inline int dlwait_wait(struct dlwait *wait)
{
    EnterCriticalSection(&wait->cs);
    SleepConditionVariableCS(&wait->cv, &wait->cs, INFINITE);
    LeaveCriticalSection(&wait->cs);
    return 0;
}

static inline int dlwait_init(struct dlwait *wait)
{
    InitializeCriticalSection(&wait->cs);
    InitializeConditionVariable(&wait->cv);
    return 0;
}

static inline int dlwait_destroy(struct dlwait *wait)
{
    DeleteCriticalSection(&wait->cs);
    return 0;
}

#else

#define dlthread_create(THREAD, ENTRY, ARG) (pthread_create(&(THREAD)->handle, NULL, ENTRY, ARG))
#define dlthread_join(THREAD) pthread_join((THREAD)->handle, NULL)
#define dlsched_yield() sched_yield()
#define dlwait_signal(WAIT) pthread_cond_signal(&(WAIT)->cv)
#define dlwait_broadcast(WAIT) pthread_cond_broadcast(&(WAIT)->cv)

static inline int dlwait_wait(struct dlwait *wait)
{
    int pr;
    if ((pr = pthread_mutex_lock(&wait->mtx)) ||
        (pr = pthread_cond_wait(&wait->cv, &wait->mtx)) ||
        (pr = pthread_mutex_unlock(&wait->mtx)))
    {
        return pr;
    }
    return 0;
}

static inline int dlwait_init(struct dlwait *wait)
{
    int pr;
    if ((pr = pthread_cond_init(&wait->cv, NULL)) ||
        (pr = pthread_mutex_init(&wait->mtx, NULL)))
    {
        return pr;
    }
    return 0;
}

static inline int dlwait_destroy(struct dlwait *wait)
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
