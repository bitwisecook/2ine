/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows critical sections own recursive host mutexes; the guest record stays 24 bytes. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "host/sync.h"

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

ULONG GetCurrentThread(void)
{
    return (ULONG)-2; /* Win32's calling-thread pseudo-handle, not an owned handle. */
}

ULONG GetCurrentThreadId(void)
{
    LxGuestTib *tib = GProcessState.getOs2Tib ? GProcessState.getOs2Tib() : NULL;
    const uint8 *tib2 =
        tib ? lx_execution_memory(GProcessState.execution, lx_guest_load_u32(&tib->tib2),
                                  sizeof(LxGuestTib2), LX_GUEST_READ)
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
    /* Mirror the host state into the guest's Win32 fields; LockCount is -1 when free. */
    uint8 record[WIN32_CRITICAL_SECTION_SIZE] = {0};
    lx_guest_store_u32(record + WIN32_CRITICAL_SECTION_LOCK_COUNT, cs->recursion + cs->waiting - 1);
    lx_guest_store_u32(record + WIN32_CRITICAL_SECTION_RECURSION_COUNT, cs->recursion);
    lx_guest_store_u32(record + WIN32_CRITICAL_SECTION_OWNING_THREAD, cs->owner);
    lx_execution_write(GProcessState.execution, cs->address, record, sizeof record);
}

VOID InitializeCriticalSection(PVOID address)
{
    uint32 pointer = (uint32)(uintptr_t)address;
    if ((uintptr_t)address > UINT32_MAX ||
        !lx_execution_memory(GProcessState.execution, pointer, WIN32_CRITICAL_SECTION_SIZE,
                             LX_GUEST_READ | LX_GUEST_WRITE))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return;
    }
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    if (find_section(pointer))
    {
        lx_host_monitor_unlock(monitor);
        open32SetError(ERROR_INVALID_PARAMETER);
        return;
    }
    CriticalSection *cs = calloc(1, sizeof *cs);
    if (cs)
        cs->mutex = lx_host_mutex_create(1, 0);
    if (!cs || !cs->mutex)
    {
        free(cs);
        lx_host_monitor_unlock(monitor);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
        !lx_execution_borrow(GProcessState.execution, pointer, WIN32_CRITICAL_SECTION_SIZE,
                             LX_GUEST_READ | LX_GUEST_WRITE, &record))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return;
    }
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    CriticalSection *cs = find_section(pointer);
    if (!cs)
    {
        lx_host_monitor_unlock(monitor);
        lx_execution_release(&record);
        open32SetError(ERROR_INVALID_PARAMETER);
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
        open32SetError(ERROR_INVALID_PARAMETER);
    store_section(cs);
    lx_host_monitor_unlock(monitor);
    lx_execution_release(&record);
}

VOID LeaveCriticalSection(PVOID address)
{
    ULONG pointer = (ULONG)(uintptr_t)address;
    if ((uintptr_t)address > UINT32_MAX ||
        !lx_execution_memory(GProcessState.execution, pointer, WIN32_CRITICAL_SECTION_SIZE,
                             LX_GUEST_READ | LX_GUEST_WRITE))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return;
    }
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    CriticalSection *cs = find_section(pointer);
    if (!cs || !cs->recursion || cs->host_owner != lx_host_thread_id())
    {
        lx_host_monitor_unlock(monitor);
        open32SetError(ERROR_NOT_LOCKED);
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
        open32SetError(cs ? ERROR_BUSY : ERROR_INVALID_PARAMETER);
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
