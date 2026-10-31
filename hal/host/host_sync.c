/* Public domain. No rights reserved. Wholly AI-authored. */
/* Synchronization adapters for host mutexes and monotonic wait deadlines. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "host_sync.h"
#include "host.h"
#include <stdlib.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
struct LxHostMutex
{
    HANDLE object;
    volatile LONG held;
};
LxHostMutex *lx_host_mutex_create(int recursive, int owned)
{
    (void)recursive;
    LxHostMutex *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    m->object = CreateMutexW(NULL, owned != 0, NULL);
    m->held = owned ? 1 : 0;
    if (!m->object)
    {
        free(m);
        return NULL;
    }
    return m;
}
LxHostLockResult lx_host_mutex_lock(LxHostMutex *m, uint32_t timeout)
{
    DWORD result = WaitForSingleObject(m->object, timeout == UINT32_MAX ? INFINITE : timeout);
    if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED)
    {
        if (result == WAIT_ABANDONED)
            InterlockedExchange(&m->held, 1);
        else
            InterlockedIncrement(&m->held);
        return result == WAIT_ABANDONED ? LX_HOST_LOCK_OWNER_DIED : LX_HOST_LOCK_OK;
    }
    return result == WAIT_TIMEOUT ? (timeout ? LX_HOST_LOCK_TIMEOUT : LX_HOST_LOCK_BUSY)
                                  : LX_HOST_LOCK_FAILED;
}
LxHostLockResult lx_host_mutex_unlock(LxHostMutex *m)
{
    if (!ReleaseMutex(m->object))
        return LX_HOST_LOCK_NOT_OWNER;
    InterlockedDecrement(&m->held);
    return LX_HOST_LOCK_OK;
}
int lx_host_mutex_busy(LxHostMutex *m)
{
    return InterlockedCompareExchange(&m->held, 0, 0) != 0;
}
LxHostLockResult lx_host_mutex_destroy(LxHostMutex *m)
{
    if (m->held)
        return LX_HOST_LOCK_BUSY;
    if (!CloseHandle(m->object))
        return LX_HOST_LOCK_FAILED;
    free(m);
    return LX_HOST_LOCK_OK;
}
struct LxHostMonitor
{
    CRITICAL_SECTION mutex;
    CONDITION_VARIABLE condition;
};
LxHostMonitor *lx_host_monitor_create(void)
{
    LxHostMonitor *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    if (!InitializeCriticalSectionEx(&m->mutex, 4000, 0))
    {
        free(m);
        return NULL;
    }
    InitializeConditionVariable(&m->condition);
    return m;
}
void lx_host_monitor_destroy(LxHostMonitor *m)
{
    DeleteCriticalSection(&m->mutex);
    free(m);
}
void lx_host_monitor_lock(LxHostMonitor *m)
{
    EnterCriticalSection(&m->mutex);
}
void lx_host_monitor_unlock(LxHostMonitor *m)
{
    LeaveCriticalSection(&m->mutex);
}
LxHostLockResult lx_host_monitor_wait(LxHostMonitor *m, uint32_t ms)
{
    if (SleepConditionVariableCS(&m->condition, &m->mutex, ms == UINT32_MAX ? INFINITE : ms))
        return LX_HOST_LOCK_OK;
    return GetLastError() == ERROR_TIMEOUT ? LX_HOST_LOCK_TIMEOUT : LX_HOST_LOCK_FAILED;
}
void lx_host_monitor_wake_all(LxHostMonitor *m)
{
    WakeAllConditionVariable(&m->condition);
}

#else
#include <pthread.h>
#include <time.h>
#include <errno.h>
struct LxHostMutex
{
    pthread_mutex_t object;
    uint32_t held, waiting;
};
LxHostMutex *lx_host_mutex_create(int recursive, int owned)
{
    LxHostMutex *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr,
                              recursive ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_ERRORCHECK);
#if defined(__linux__)
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
#endif
    int rc = pthread_mutex_init(&m->object, &attr);
    pthread_mutexattr_destroy(&attr);
    if (rc)
    {
        free(m);
        return NULL;
    }
    if (owned && pthread_mutex_lock(&m->object))
    {
        pthread_mutex_destroy(&m->object);
        free(m);
        return NULL;
    }
    m->held = owned ? 1 : 0;
    return m;
}
static LxHostLockResult result(int rc)
{
    switch (rc)
    {
        case 0:
            return LX_HOST_LOCK_OK;
        case EBUSY:
            return LX_HOST_LOCK_BUSY;
        case ETIMEDOUT:
            return LX_HOST_LOCK_TIMEOUT;
        case EPERM:
            return LX_HOST_LOCK_NOT_OWNER;
#if defined(EOWNERDEAD)
        case EOWNERDEAD:
            return LX_HOST_LOCK_OWNER_DIED;
#endif
        default:
            return LX_HOST_LOCK_FAILED;
    }
}
static void stopWaiting(void *p)
{
    LxHostMutex *m = p;
    __atomic_sub_fetch(&m->waiting, 1, __ATOMIC_SEQ_CST);
}
LxHostLockResult lx_host_mutex_lock(LxHostMutex *m, uint32_t timeout)
{
    int rc;
    __atomic_add_fetch(&m->waiting, 1, __ATOMIC_SEQ_CST);
    pthread_cleanup_push(stopWaiting, m);
    if (timeout == UINT32_MAX)
        rc = pthread_mutex_lock(&m->object);
    else
    {
        const uint64_t deadline = lx_host_monotonic_us() + (uint64_t)timeout * 1000;
        while ((rc = pthread_mutex_trylock(&m->object)) == EBUSY && timeout)
        {
            if (lx_host_monotonic_us() >= deadline)
            {
                rc = ETIMEDOUT;
                break;
            }
            const struct timespec pause = {0, 1000000};
            nanosleep(&pause, NULL);
        }
    }
#if defined(__linux__)
    if (rc == EOWNERDEAD)
        pthread_mutex_consistent(&m->object);
#endif
    if (rc == 0)
        __atomic_add_fetch(&m->held, 1, __ATOMIC_SEQ_CST);
#if defined(EOWNERDEAD)
    if (rc == EOWNERDEAD)
        __atomic_store_n(&m->held, 1, __ATOMIC_SEQ_CST);
#endif
    pthread_cleanup_pop(1);
    return result(rc);
}
LxHostLockResult lx_host_mutex_unlock(LxHostMutex *m)
{
    int rc = pthread_mutex_unlock(&m->object);
    if (!rc)
        __atomic_sub_fetch(&m->held, 1, __ATOMIC_SEQ_CST);
    return result(rc);
}
int lx_host_mutex_busy(LxHostMutex *m)
{
    return __atomic_load_n(&m->held, __ATOMIC_SEQ_CST) ||
           __atomic_load_n(&m->waiting, __ATOMIC_SEQ_CST);
}
LxHostLockResult lx_host_mutex_destroy(LxHostMutex *m)
{
    if (lx_host_mutex_busy(m))
        return LX_HOST_LOCK_BUSY;
    int rc = pthread_mutex_destroy(&m->object);
    if (!rc)
        free(m);
    return result(rc);
}

struct LxHostMonitor
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    clockid_t clock;
};
LxHostMonitor *lx_host_monitor_create(void)
{
    LxHostMonitor *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    if (pthread_mutex_init(&m->mutex, NULL))
    {
        free(m);
        return NULL;
    }
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    m->clock = CLOCK_REALTIME;
#if defined(__linux__)
    if (!pthread_condattr_setclock(&attr, CLOCK_MONOTONIC))
        m->clock = CLOCK_MONOTONIC;
#endif
    int rc = pthread_cond_init(&m->condition, &attr);
    pthread_condattr_destroy(&attr);
    if (rc)
    {
        pthread_mutex_destroy(&m->mutex);
        free(m);
        return NULL;
    }
    return m;
}
void lx_host_monitor_destroy(LxHostMonitor *m)
{
    pthread_cond_destroy(&m->condition);
    pthread_mutex_destroy(&m->mutex);
    free(m);
}
void lx_host_monitor_lock(LxHostMonitor *m)
{
    pthread_mutex_lock(&m->mutex);
}
void lx_host_monitor_unlock(LxHostMonitor *m)
{
    pthread_mutex_unlock(&m->mutex);
}
static void unlockMonitor(void *p)
{
    lx_host_monitor_unlock(p);
}
LxHostLockResult lx_host_monitor_wait(LxHostMonitor *m, uint32_t ms)
{
    int rc;
    pthread_cleanup_push(unlockMonitor, m);
    if (ms == UINT32_MAX)
        rc = pthread_cond_wait(&m->condition, &m->mutex);
    else
    {
#if defined(__APPLE__)
        struct timespec duration = {ms / 1000, (long)(ms % 1000) * 1000000};
        rc = pthread_cond_timedwait_relative_np(&m->condition, &m->mutex, &duration);
#else
        struct timespec deadline;
        clock_gettime(m->clock, &deadline);
        deadline.tv_sec += ms / 1000;
        deadline.tv_nsec += (long)(ms % 1000) * 1000000;
        if (deadline.tv_nsec >= 1000000000)
        {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000;
        }
        rc = pthread_cond_timedwait(&m->condition, &m->mutex, &deadline);
#endif
    }
    pthread_cleanup_pop(0);
    return result(rc);
}
void lx_host_monitor_wake_all(LxHostMonitor *m)
{
    pthread_cond_broadcast(&m->condition);
}
#endif

void lx_host_once(LxHostOnce *once, void (*initialize)(void))
{
#if defined(_WIN32)
    LONG state = InterlockedCompareExchange((volatile LONG *)&once->state, 1, 0);
    if (!state)
    {
        initialize();
        InterlockedExchange((volatile LONG *)&once->state, 2);
    }
    else
        while (InterlockedCompareExchange((volatile LONG *)&once->state, 2, 2) != 2)
            Sleep(0);
#else
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(&once->state, &expected, 1, 0, __ATOMIC_ACQUIRE,
                                    __ATOMIC_RELAXED))
    {
        initialize();
        __atomic_store_n(&once->state, 2, __ATOMIC_RELEASE);
    }
    else
        while (__atomic_load_n(&once->state, __ATOMIC_ACQUIRE) != 2)
        {
            const struct timespec pause = {0, 1000000};
            nanosleep(&pause, NULL);
        }
#endif
}
