/* Public domain. No rights reserved. Wholly AI-authored. */
/* Synchronization adapters for host mutexes and monotonic wait deadlines. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "hal/host_sync.h"
#include "hal/host.h"
#include <stdlib.h>
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

void lx_host_once(LxHostOnce *once, void (*initialize)(void))
{
    LONG state = InterlockedCompareExchange((volatile LONG *)&once->state, 1, 0);
    if (!state)
    {
        initialize();
        InterlockedExchange((volatile LONG *)&once->state, 2);
    }
    else
        while (InterlockedCompareExchange((volatile LONG *)&once->state, 2, 2) != 2)
            Sleep(0);
}
