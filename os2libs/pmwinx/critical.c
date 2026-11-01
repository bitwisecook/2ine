/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows critical sections own recursive host mutexes; the guest record stays 24 bytes. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "hal/host_sync.h"
typedef struct CriticalSection
{
    ULONG address, owner, recursion, waiting;
    uint64_t host_owner;
    LxHostMutex *mutex;
    struct CriticalSection *next;
} CriticalSection;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *monitor;
static CriticalSection *sections;
static void initialize(void)
{
    monitor = lx_host_monitor_create();
    if (!monitor)
        abort();
}
ULONG GetCurrentThreadId(void)
{
    LxTIB *tib = GLoaderState.getOs2Tib ? GLoaderState.getOs2Tib() : NULL;
    const uint8 *tib2 = tib ? lx_execution_memory(GLoaderState.execution, tib->tib_ptib2,
                                                  sizeof(LxTIB2), LX_GUEST_READ)
                            : NULL;
    return tib2 ? lx_guest_load_u32(tib2) : 0;
}
static CriticalSection *find_section(ULONG address)
{
    for (CriticalSection *cs = sections; cs; cs = cs->next)
        if (cs->address == address)
            return cs;
    return NULL;
}
static void store_section(CriticalSection *cs)
{
    uint8 record[24] = {0};
    lx_guest_store_u32(record + 4, cs->recursion + cs->waiting - 1);
    lx_guest_store_u32(record + 8, cs->recursion);
    lx_guest_store_u32(record + 12, cs->owner);
    lx_execution_write(GLoaderState.execution, cs->address, record, sizeof record);
}
VOID InitializeCriticalSection(PVOID address)
{
    uint32 pointer = (uint32)(uintptr_t)address;
    if ((uintptr_t)address > UINT32_MAX ||
        !lx_execution_memory(GLoaderState.execution, pointer, 24, 3))
    {
        open32SetError(87);
        return;
    }
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    if (find_section(pointer))
    {
        lx_host_monitor_unlock(monitor);
        open32SetError(87);
        return;
    }
    CriticalSection *cs = calloc(1, sizeof *cs);
    if (cs)
        cs->mutex = lx_host_mutex_create(1, 0);
    if (!cs || !cs->mutex)
    {
        free(cs);
        lx_host_monitor_unlock(monitor);
        open32SetError(8);
        return;
    }
    cs->address = pointer;
    cs->next = sections;
    sections = cs;
    store_section(cs);
    lx_host_monitor_unlock(monitor);
}
VOID EnterCriticalSection(PVOID address)
{
    LxGuestBorrow record = {0};
    ULONG pointer = (ULONG)(uintptr_t)address;
    if ((uintptr_t)address > UINT32_MAX ||
        !lx_execution_borrow(GLoaderState.execution, pointer, 24, 3, &record))
    {
        open32SetError(87);
        return;
    }
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    CriticalSection *cs = find_section(pointer);
    if (!cs)
    {
        lx_host_monitor_unlock(monitor);
        lx_execution_release(&record);
        open32SetError(87);
        return;
    }
    cs->waiting++;
    store_section(cs);
    lx_host_monitor_unlock(monitor);
    LxHostLockResult result = lx_host_mutex_lock(cs->mutex, UINT32_MAX);
    lx_host_monitor_lock(monitor);
    cs->waiting--;
    if (result == LX_HOST_LOCK_OK)
    {
        cs->recursion++;
        cs->host_owner = lx_host_thread_id();
        cs->owner = GetCurrentThreadId();
    }
    else
        open32SetError(87);
    store_section(cs);
    lx_host_monitor_unlock(monitor);
    lx_execution_release(&record);
}
VOID LeaveCriticalSection(PVOID address)
{
    ULONG pointer = (ULONG)(uintptr_t)address;
    if ((uintptr_t)address > UINT32_MAX ||
        !lx_execution_memory(GLoaderState.execution, pointer, 24, 3))
    {
        open32SetError(87);
        return;
    }
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    CriticalSection *cs = find_section(pointer);
    if (!cs || !cs->recursion || cs->host_owner != lx_host_thread_id())
    {
        lx_host_monitor_unlock(monitor);
        open32SetError(158);
        return;
    }
    cs->recursion--;
    if (!cs->recursion)
    {
        cs->owner = 0;
        cs->host_owner = 0;
    }
    store_section(cs);
    lx_host_mutex_unlock(cs->mutex);
    lx_host_monitor_unlock(monitor);
}
VOID DeleteCriticalSection(PVOID address)
{
    ULONG pointer = (ULONG)(uintptr_t)address;
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    CriticalSection **link = &sections;
    while (*link && (*link)->address != pointer)
        link = &(*link)->next;
    CriticalSection *cs = *link;
    if (!cs || (uintptr_t)address > UINT32_MAX || cs->recursion || cs->waiting)
    {
        lx_host_monitor_unlock(monitor);
        open32SetError(cs ? 170 : 87);
        return;
    }
    *link = cs->next;
    lx_host_mutex_destroy(cs->mutex);
    free(cs);
    lx_host_monitor_unlock(monitor);
}
void open32CriticalCleanup(void)
{
    while (sections)
    {
        CriticalSection *cs = sections;
        sections = cs->next;
        if (!cs->recursion && !cs->waiting)
        {
            lx_host_mutex_destroy(cs->mutex);
            free(cs);
        }
    }
}
