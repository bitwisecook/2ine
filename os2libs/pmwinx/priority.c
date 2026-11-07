/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 process classes retain their public value after a host scheduling change.
   A child's class is applied to the child and kept on its process object. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/process.h"
#include "hal/host_process.h"
#include "hal/host_sync.h"

enum
{
    OPEN32_NORMAL_PRIORITY_CLASS = 0x20,
    OPEN32_IDLE_PRIORITY_CLASS = 0x40,
    OPEN32_HIGH_PRIORITY_CLASS = 0x80,
    OPEN32_REALTIME_PRIORITY_CLASS = 0x100,
    OPEN32_ERROR_ACCESS_DENIED = 5,
    OPEN32_ERROR_INVALID_HANDLE = 6,
    OPEN32_ERROR_NOT_ENOUGH_MEMORY = 8,
    OPEN32_ERROR_GEN_FAILURE = 31,
    OPEN32_ERROR_NOT_SUPPORTED = 50,
    OPEN32_ERROR_INVALID_PARAMETER = 87,
    OPEN32_ERROR_INVALID_PROCID = 303,
    OPEN32_ERROR_INVALID_PRIORITY = 307
};
static LxHostOnce priority_once = LX_HOST_ONCE_INIT;
static LxHostMonitor *priority_monitor;
static ULONG process_priority = OPEN32_NORMAL_PRIORITY_CLASS;

static void initialize_priority(void)
{
    priority_monitor = lx_host_monitor_create();
}

static ULONG priority_error(int error)
{
    switch (error)
    {
        case EACCES:
        case EPERM:
            return OPEN32_ERROR_ACCESS_DENIED;
        case ENOTSUP:
        case ENOSYS:
            return OPEN32_ERROR_NOT_SUPPORTED;
        case ENOMEM:
            return OPEN32_ERROR_NOT_ENOUGH_MEMORY;
        case ESRCH:
            return OPEN32_ERROR_INVALID_PROCID;
        case EINVAL:
            return OPEN32_ERROR_INVALID_PARAMETER;
        default:
            return OPEN32_ERROR_GEN_FAILURE;
    }
}

BOOL SetPriorityClass(ULONG process, ULONG priority)
{
    LxHostProcessPriority selected;
    switch (priority)
    {
        case OPEN32_NORMAL_PRIORITY_CLASS:
        case OPEN32_HIGH_PRIORITY_CLASS:
            /* Original Open32 maps both public classes to OS/2 regular, delta 0. */
            selected = LX_HOST_PROCESS_PRIORITY_NORMAL;
            break;
        case OPEN32_IDLE_PRIORITY_CLASS:
            selected = LX_HOST_PROCESS_PRIORITY_IDLE;
            break;
        case OPEN32_REALTIME_PRIORITY_CLASS:
            selected = LX_HOST_PROCESS_PRIORITY_REALTIME;
            break;
        default:
            open32SetError(OPEN32_ERROR_INVALID_PRIORITY);
            return FALSE;
    }
    /* The class is validated before the handle, matching the original's order. */
    int child_error = 0;
    int target = open32ChildSetPriority(process, selected, priority, &child_error);
    if (target < 0)
    {
        open32SetError(OPEN32_ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (target)
    {
        if (child_error)
            open32SetError(priority_error(child_error));
        return !child_error;
    }
    lx_host_once(&priority_once, initialize_priority);
    if (!priority_monitor)
    {
        open32SetError(OPEN32_ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    lx_host_monitor_lock(priority_monitor);
    int result = lx_host_process_set_priority(selected);
    int error = errno;
    if (!result)
        __atomic_store_n(&process_priority, priority, __ATOMIC_RELEASE);
    lx_host_monitor_unlock(priority_monitor);
    if (result)
        open32SetError(priority_error(error));
    return result == 0;
}

ULONG GetPriorityClass(ULONG process)
{
    ULONG child_class = 0;
    int target = open32ChildPriorityClass(process, &child_class);
    if (target < 0)
    {
        open32SetError(OPEN32_ERROR_INVALID_HANDLE);
        return 0;
    }
    return target ? child_class : __atomic_load_n(&process_priority, __ATOMIC_ACQUIRE);
}
