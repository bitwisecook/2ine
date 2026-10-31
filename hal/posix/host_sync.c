/* Public domain. No rights reserved. Wholly AI-authored. */
/* Synchronization adapters for host mutexes and monotonic wait deadlines. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "host_sync.h"
#include "sync_platform.h"
#include "host.h"
#include <stdlib.h>
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
    lx_posix_mutex_attributes(&attr);
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
    lx_posix_mutex_recover(&m->object, rc);
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
    m->clock = lx_posix_condition_clock(&attr);
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
        rc = lx_posix_condition_timedwait(&m->condition, &m->mutex, m->clock, ms);
    pthread_cleanup_pop(0);
    return result(rc);
}
void lx_host_monitor_wake_all(LxHostMonitor *m)
{
    pthread_cond_broadcast(&m->condition);
}

void lx_host_once(LxHostOnce *once, void (*initialize)(void))
{
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
}
