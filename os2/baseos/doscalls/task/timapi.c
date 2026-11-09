/* Public domain. No rights reserved. Wholly AI-authored. */
/* Interval timers posting shared event semaphores from one host thread.
   A timer keeps its semaphore alive for its whole life, like the open the OS/2
   kernel holds, so closing the application's handle does not stop it. */
#include "os2/baseos/doscalls/internal.h"

enum
{
    TIMER_HANDLE_LIMIT = 4095
};

typedef struct Timer
{
    struct Timer *next;
    LxHandleBorrow semaphore;
    uint64 due, interval;
    HEV event;
    uint16 handle;
    BOOL one_shot, firing;
} Timer;

static LxHostOnce monitor_once = LX_HOST_ONCE_INIT;
static LxHostMonitor *monitor;
static LxHostThread *worker;
static Timer *timers;
static BOOL stopping;

static void initializeMonitor(void)
{
    monitor = lx_host_monitor_create();
    if (!monitor)
        abort();
}

static void unlinkTimer(Timer *timer)
{
    for (Timer **link = &timers; *link; link = &(*link)->next)
        if (*link == timer)
        {
            *link = timer->next;
            return;
        }
}

static void destroyTimer(Timer *timer)
{
    lx_handle_release(&timer->semaphore);
    free(timer);
}

static Timer *earliestTimer(void)
{
    Timer *earliest = NULL;
    for (Timer *timer = timers; timer; timer = timer->next)
        if (!earliest || timer->due < earliest->due)
            earliest = timer;
    return earliest;
}

static void *timerWorker(void *unused)
{
    (void)unused;
    lx_host_monitor_lock(monitor);
    while (!stopping)
    {
        Timer *timer = earliestTimer();
        if (!timer)
        {
            lx_host_monitor_wait(monitor, UINT32_MAX);
            continue;
        }
        const uint64 now = lx_host_monotonic_us();
        if (timer->due > now)
        {
            const uint64 wait = (timer->due - now + US_PER_MS - 1) / US_PER_MS;
            lx_host_monitor_wait(monitor, wait < INT32_MAX ? (uint32)wait : INT32_MAX);
            continue;
        }
        timer->firing = TRUE;
        lx_host_monitor_unlock(monitor);
        /* OS/2 ignores the post result, including an already posted semaphore. */
        DosPostEventSemResource(timer->event, timer->semaphore.resource);
        lx_host_monitor_lock(monitor);
        timer->firing = FALSE;
        lx_host_monitor_wake_all(monitor);
        if (timer->one_shot)
        {
            unlinkTimer(timer);
            lx_host_monitor_unlock(monitor);
            destroyTimer(timer);
            lx_host_monitor_lock(monitor);
        }
        else
            timer->due = lx_host_monotonic_us() + timer->interval;
    }
    lx_host_monitor_unlock(monitor);
    return NULL;
}

static int freeHandleLocked(uint16 *handle)
{
    for (uint32 candidate = 0; candidate < TIMER_HANDLE_LIMIT; candidate++)
    {
        const Timer *timer = timers;
        while (timer && timer->handle != candidate)
            timer = timer->next;
        if (!timer)
        {
            *handle = (uint16)candidate;
            return 1;
        }
    }
    return 0;
}

static APIRET armTimer(ULONG msec, HSEM hsem, PHTIMER output, BOOL one_shot)
{
    Timer *timer = calloc(1, sizeof *timer);
    if (!timer)
        return ERROR_TS_NOTIMER;
    if (!borrowEventResource((HEV)hsem, &timer->semaphore))
    {
        free(timer);
        return ERROR_TS_SEMHANDLE;
    }
    const EventSem *event = timer->semaphore.resource;
    if (!event->shared_state && !semaphoreResourceShared(timer->semaphore.resource))
    {
        destroyTimer(timer);
        return ERROR_TS_SEMHANDLE;
    }
    timer->event = (HEV)hsem;
    timer->one_shot = one_shot;
    /* A zero interval posts each millisecond instead of spinning. */
    timer->interval = (uint64)(msec ? msec : 1) * US_PER_MS;
    timer->due = lx_host_monotonic_us() + timer->interval;
    lx_host_once(&monitor_once, initializeMonitor);
    lx_host_monitor_lock(monitor);
    if (!stopping && !worker)
        worker = lx_host_thread_create(timerWorker, NULL, NULL);
    if (stopping || !worker || !freeHandleLocked(&timer->handle))
    {
        lx_host_monitor_unlock(monitor);
        destroyTimer(timer);
        return ERROR_TS_NOTIMER;
    }
    timer->next = timers;
    timers = timer;
    /* The original stores a word; the upper half of a ULONG output is untouched. */
    if (output)
        lx_guest_store_u16((uint8 *)output, timer->handle);
    lx_host_monitor_wake_all(monitor);
    lx_host_monitor_unlock(monitor);
    return NO_ERROR;
}

APIRET DosAsyncTimer(ULONG msec, HSEM hsem, PHTIMER phtimer)
{
    TRACE_NATIVE("DosAsyncTimer(%u, %u, %p)", (uint)msec, (uint)hsem, phtimer);
    return armTimer(msec, hsem, phtimer, TRUE);
}

APIRET DosStartTimer(ULONG msec, HSEM hsem, PHTIMER phtimer)
{
    TRACE_NATIVE("DosStartTimer(%u, %u, %p)", (uint)msec, (uint)hsem, phtimer);
    return armTimer(msec, hsem, phtimer, FALSE);
}

APIRET DosStopTimer(HTIMER htimer)
{
    TRACE_NATIVE("DosStopTimer(%u)", (uint)htimer);
    const uint16 handle = (uint16)htimer;
    lx_host_once(&monitor_once, initializeMonitor);
    lx_host_monitor_lock(monitor);
    for (;;)
    {
        Timer *timer = timers;
        while (timer && timer->handle != handle)
            timer = timer->next;
        if (!timer)
        {
            lx_host_monitor_unlock(monitor);
            return ERROR_TS_HANDLE;
        }
        if (timer->firing)
        {
            lx_host_monitor_wait(monitor, UINT32_MAX);
            continue;
        }
        unlinkTimer(timer);
        lx_host_monitor_wake_all(monitor);
        lx_host_monitor_unlock(monitor);
        destroyTimer(timer);
        return NO_ERROR;
    }
}

void cleanupTimers(void)
{
    lx_host_once(&monitor_once, initializeMonitor);
    lx_host_monitor_lock(monitor);
    stopping = TRUE;
    lx_host_monitor_wake_all(monitor);
    LxHostThread *thread = worker;
    worker = NULL;
    lx_host_monitor_unlock(monitor);
    if (thread)
        lx_host_thread_join(thread, NULL);
    while (timers)
    {
        Timer *timer = timers;
        timers = timer->next;
        destroyTimer(timer);
    }
}
