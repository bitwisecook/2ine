/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host synchronization objects. OS/2 ownership and handles live above this API. */
#ifndef TWOINE_HOST_SYNC_H
#define TWOINE_HOST_SYNC_H
#include <stdint.h>
typedef struct LxHostMutex LxHostMutex;
typedef struct LxHostOnce
{
    uint32_t state;
} LxHostOnce;
#define LX_HOST_ONCE_INIT {0}
void lx_host_once(LxHostOnce *once, void (*initialize)(void));
typedef enum LxHostLockResult
{
    LX_HOST_LOCK_OK,
    LX_HOST_LOCK_BUSY,
    LX_HOST_LOCK_TIMEOUT,
    LX_HOST_LOCK_OWNER_DIED,
    LX_HOST_LOCK_NOT_OWNER,
    LX_HOST_LOCK_FAILED
} LxHostLockResult;
LxHostMutex *lx_host_mutex_create(int recursive, int owned);
/* UINT32_MAX waits indefinitely; zero never waits. */
LxHostLockResult lx_host_mutex_lock(LxHostMutex *mutex, uint32_t timeout_ms);
LxHostLockResult lx_host_mutex_unlock(LxHostMutex *mutex);
/* A busy object remains valid. Success releases it. */
int lx_host_mutex_busy(LxHostMutex *mutex);
LxHostLockResult lx_host_mutex_destroy(LxHostMutex *mutex);
typedef struct LxHostMonitor LxHostMonitor;
LxHostMonitor *lx_host_monitor_create(void);
void lx_host_monitor_destroy(LxHostMonitor *monitor);
void lx_host_monitor_lock(LxHostMonitor *monitor);
void lx_host_monitor_unlock(LxHostMonitor *monitor);
/* Wait releases the monitor atomically, returning with it held. Cancellation
   releases it. UINT32_MAX is indefinite; finite waits use monotonic durations. */
LxHostLockResult lx_host_monitor_wait(LxHostMonitor *monitor, uint32_t timeout_ms);
void lx_host_monitor_wake_all(LxHostMonitor *monitor);
#endif
