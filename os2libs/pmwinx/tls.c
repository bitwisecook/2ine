/* Public domain. No rights reserved. Wholly AI-authored. */
/* Process-owned Open32 TLS indices hold opaque 32-bit values on each host thread. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "hal/host_sync.h"
#include "hal/host_thread.h"
#define OPEN32_TLS_SLOTS 64
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *monitor;
static unsigned char allocated[OPEN32_TLS_SLOTS];
static uint64 generations[OPEN32_TLS_SLOTS];

typedef struct ThreadSlots
{
    ULONG values[OPEN32_TLS_SLOTS];
    uint64 generations[OPEN32_TLS_SLOTS];
} ThreadSlots;

/* Fixed TLS storage is released by the host when its thread exits. A generation
   change clears reused indices lazily, including values on other live threads. */
static LX_HOST_THREAD_LOCAL ThreadSlots thread_slots;

static void initialize(void)
{
    monitor = lx_host_monitor_create();
}

static BOOL enter(void)
{
    lx_host_once(&once, initialize);
    if (!monitor)
    {
        open32SetError(8);
        return FALSE;
    }
    lx_host_monitor_lock(monitor);
    return TRUE;
}

ULONG TlsAlloc(void)
{
    TRACE_NATIVE("TlsAlloc()");
    if (!enter())
        return UINT32_MAX;
    for (unsigned n = 0; n < OPEN32_TLS_SLOTS; n++)
    {
        unsigned index = (n >> 1) + ((n & 1) ? 32 : 0);
        if (!allocated[index])
        {
            allocated[index] = 1;
            generations[index]++;
            lx_host_monitor_unlock(monitor);
            return index;
        }
    }
    lx_host_monitor_unlock(monitor);
    open32SetError(8);
    return UINT32_MAX;
}

BOOL TlsFree(ULONG index)
{
    TRACE_NATIVE("TlsFree(%u)", (uint)index);
    if (index >= OPEN32_TLS_SLOTS)
    {
        open32SetError(87);
        return FALSE;
    }
    if (!enter())
        return FALSE;
    allocated[index] = 0;
    lx_host_monitor_unlock(monitor);
    return TRUE;
}

ULONG TlsGetValue(ULONG index)
{
    TRACE_NATIVE("TlsGetValue(%u)", (uint)index);
    if (index >= OPEN32_TLS_SLOTS)
    {
        open32SetError(87);
        return 0;
    }
    if (!enter())
        return 0;
    ULONG result =
        thread_slots.generations[index] == generations[index] ? thread_slots.values[index] : 0;
    lx_host_monitor_unlock(monitor);
    SetLastError(0);
    return result;
}

BOOL TlsSetValue(ULONG index, ULONG value)
{
    TRACE_NATIVE("TlsSetValue(%u, %X)", (uint)index, (uint)value);
    if (index >= OPEN32_TLS_SLOTS)
    {
        open32SetError(87);
        return FALSE;
    }
    if (!enter())
        return FALSE;
    thread_slots.values[index] = value;
    thread_slots.generations[index] = generations[index];
    lx_host_monitor_unlock(monitor);
    return TRUE;
}

void open32TlsCleanup(void)
{
    if (!monitor)
        return;
    /* Native-module shutdown is quiescent, as for other Open32 resources. */
    lx_host_monitor_lock(monitor);
    for (unsigned index = 0; index < OPEN32_TLS_SLOTS; index++)
    {
        allocated[index] = 0;
        generations[index]++;
    }
    lx_host_monitor_unlock(monitor);
    lx_host_monitor_destroy(monitor);
    monitor = NULL;
    once = (LxHostOnce)LX_HOST_ONCE_INIT;
}
