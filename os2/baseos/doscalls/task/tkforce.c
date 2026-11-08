/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2/baseos/doscalls/internal.h"

/* Suspension state has one leaf monitor: nothing else is locked while it is held,
   so it cannot order against GMutexDosCalls or the CPU lock. Its list covers the
   primary, DosCreateThread and Dos16CreateThread threads alike. */
enum
{
    SUSPEND_POLL_MS = 10
};

static LxHostMonitor *suspend_monitor;

static ThreadControl *thread_controls;

static ThreadControl primary_control = {NULL, 1, 0};

static uint32 suspended_threads;

static ThreadControl *findThreadControl(uint32 id)
{
    ThreadControl *control = thread_controls;
    while (control && control->id != id)
        control = control->next;
    return control;
}

static void setControlSuspended(ThreadControl *control, int suspended)
{
    if (control->suspended == suspended)
        return;
    __atomic_store_n(&control->suspended, suspended, __ATOMIC_RELEASE);
    __atomic_add_fetch(&suspended_threads, suspended ? 1 : (uint32)-1, __ATOMIC_ACQ_REL);
}

ThreadControl *createThreadControl(uint32 id, BOOL suspended)
{
    ThreadControl *control = calloc(1, sizeof *control);
    if (!control)
        return NULL;
    control->id = id;
    lx_host_monitor_lock(suspend_monitor);
    control->next = thread_controls;
    thread_controls = control;
    setControlSuspended(control, suspended != 0);
    lx_host_monitor_unlock(suspend_monitor);
    return control;
}

void bindThreadControl(ThreadControl *control)
{
    lx_execution_set_park_flag(GProcessState.execution, &control->suspended);
}

void removeThreadControl(ThreadControl *control)
{
    if (!control)
        return;
    lx_host_monitor_lock(suspend_monitor);
    setControlSuspended(control, 0);
    for (ThreadControl **link = &thread_controls; *link; link = &(*link)->next)
        if (*link == control)
        {
            *link = control->next;
            break;
        }
    lx_host_monitor_unlock(suspend_monitor);
    free(control);
}

/* The record belongs to the calling thread, so it stays valid while parked.
   Waits are bounded because process termination must still reach a parked thread:
   thread one runs the exit list, and any other thread stops for good. */
static void suspensionCheckpoint(void *unused)
{
    (void)unused;
    if (!__atomic_load_n(&suspended_threads, __ATOMIC_ACQUIRE) || !LX_GETTIB())
        return;
    const uint32 id = ((PTIB2)LX_GETTIB2())->tib2_ultid;
    lx_host_monitor_lock(suspend_monitor);
    ThreadControl *control = findThreadControl(id);
    while (control && control->suspended)
    {
        lx_host_monitor_wait(suspend_monitor, SUSPEND_POLL_MS);
        lx_host_monitor_unlock(suspend_monitor);
        lx_execution_poll_exit(GProcessState.execution);
        lx_host_monitor_lock(suspend_monitor);
    }
    lx_host_monitor_unlock(suspend_monitor);
}

void initializeThreadSuspension(void)
{
    suspend_monitor = lx_host_monitor_create();
    if (!suspend_monitor)
        abort();
    thread_controls = &primary_control;
    bindThreadControl(&primary_control);
    lx_execution_set_checkpoint(GProcessState.execution, suspensionCheckpoint, NULL);
}

/* Exit processing runs guest code on thread one, so suspension ends here. Other
   threads resume only as far as the termination poll, which holds them. */
void releaseThreadSuspension(void)
{
    lx_host_monitor_lock(suspend_monitor);
    for (ThreadControl *control = thread_controls; control; control = control->next)
        setControlSuspended(control, 0);
    lx_host_monitor_wake_all(suspend_monitor);
    lx_host_monitor_unlock(suspend_monitor);
}

void cleanupThreadSuspension(void)
{
    lx_execution_set_park_flag(GProcessState.execution, NULL);
    lx_execution_set_checkpoint(GProcessState.execution, NULL, NULL);
    /* Guest threads held for process exit can still be inside the monitor. */
    if (__atomic_load_n(&GProcessState.ne_threads_started, __ATOMIC_ACQUIRE))
        return;
    lx_host_monitor_destroy(suspend_monitor);
    suspend_monitor = NULL;
    thread_controls = NULL;
}

/* Suspension is one flag rather than a count, and suspending is idempotent. */
APIRET DosSuspendThread(TID id)
{
    TRACE_NATIVE("DosSuspendThread(%u)", (uint)id);
    /* Direct execution has no boundary at which to stop running guest code. */
    if (!strcmp(lx_execution_backend(GProcessState.execution), "native"))
        return ERROR_NOT_SUPPORTED;
    lx_host_monitor_lock(suspend_monitor);
    ThreadControl *control = findThreadControl(id);
    if (!control)
    {
        lx_host_monitor_unlock(suspend_monitor);
        return ERROR_INVALID_THREADID;
    }
    setControlSuspended(control, 1);
    lx_host_monitor_unlock(suspend_monitor);
    /* After this the target runs no guest instruction until it is resumed. */
    lx_execution_quiesce(GProcessState.execution);
    if (LX_GETTIB() && ((PTIB2)LX_GETTIB2())->tib2_ultid == id)
        suspensionCheckpoint(NULL);
    return NO_ERROR;
}

APIRET DosResumeThread(TID id)
{
    TRACE_NATIVE("DosResumeThread(%u)", (uint)id);
    lx_host_monitor_lock(suspend_monitor);
    ThreadControl *control = findThreadControl(id);
    APIRET result = !control             ? ERROR_INVALID_THREADID
                    : control->suspended ? NO_ERROR
                                         : ERROR_NOT_FROZEN;
    if (!result)
    {
        setControlSuspended(control, 0);
        lx_host_monitor_wake_all(suspend_monitor);
    }
    lx_host_monitor_unlock(suspend_monitor);
    return result;
}
