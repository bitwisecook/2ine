/* Public domain. No rights reserved. Wholly AI-authored. */
/* Switch entries belong to this worker; no host desktop task list is modified. */
#include "os2libs/os2native.h"
#include "os2libs/pmshapi.h"
#include "guest/handles.h"
#include "hal/host_sync.h"
#include "hal/host.h"

typedef struct SwitchEntry
{
    struct SwitchEntry *next;
    ULONG handle;
    SWCNTRL value;
} SwitchEntry;
static SwitchEntry *entries;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *mutex;
static void initialize(void)
{
    mutex = lx_host_monitor_create();
    if (!mutex)
        abort();
}
static void destroy(void *resource)
{
    SwitchEntry *entry = resource;
    lx_host_monitor_lock(mutex);
    SwitchEntry **link = &entries;
    while (*link && *link != entry)
        link = &(*link)->next;
    if (*link)
        *link = entry->next;
    lx_host_monitor_unlock(mutex);
    free(entry);
}
static BOOL valid(const SWCNTRL *entry)
{
    uint32 process, parent;
    lx_host_process_ids(&process, &parent);
    return entry && entry->hwnd && (!entry->idProcess || entry->idProcess == process) &&
           (entry->uchVisibility == 1 || entry->uchVisibility == 2 || entry->uchVisibility == 4) &&
           (entry->fbJump == 1 || entry->fbJump == 2) &&
           memchr(entry->szSwtitle, 0, sizeof entry->szSwtitle);
}
ULONG WinAddSwitchEntry(PSWCNTRL value)
{
    TRACE_NATIVE("WinAddSwitchEntry(%p)", value);
    if (!valid(value))
        return 0;
    lx_host_once(&once, initialize);
    SwitchEntry *entry = calloc(1, sizeof *entry);
    if (!entry)
        return 0;
    entry->value = *value;
    if (!entry->value.idProcess)
    {
        uint32 parent;
        lx_host_process_ids(&entry->value.idProcess, &parent);
    }
    entry->handle = lx_handle_create(LX_HANDLE_SWITCH, entry, destroy);
    if (!entry->handle)
    {
        free(entry);
        return 0;
    }
    lx_host_monitor_lock(mutex);
    entry->next = entries;
    entries = entry;
    lx_host_monitor_unlock(mutex);
    return entry->handle;
}
ULONG WinRemoveSwitchEntry(ULONG handle)
{
    TRACE_NATIVE("WinRemoveSwitchEntry(%u)", (uint)handle);
    return lx_handle_close(handle, LX_HANDLE_SWITCH) ? NO_ERROR : ERROR_INVALID_HANDLE;
}
ULONG WinChangeSwitchEntry(ULONG handle, PSWCNTRL value)
{
    TRACE_NATIVE("WinChangeSwitchEntry(%u, %p)", (uint)handle, value);
    if (!valid(value))
        return ERROR_INVALID_PARAMETER;
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_SWITCH, &borrow))
        return ERROR_INVALID_HANDLE;
    lx_host_monitor_lock(mutex);
    SwitchEntry *entry = borrow.resource;
    ULONG process = entry->value.idProcess;
    entry->value = *value;
    if (!entry->value.idProcess)
        entry->value.idProcess = process;
    lx_host_monitor_unlock(mutex);
    lx_handle_release(&borrow);
    return NO_ERROR;
}
ULONG WinQuerySwitchEntry(ULONG handle, PSWCNTRL value)
{
    if (!value)
        return ERROR_INVALID_PARAMETER;
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_SWITCH, &borrow))
        return ERROR_INVALID_HANDLE;
    lx_host_monitor_lock(mutex);
    *value = ((SwitchEntry *)borrow.resource)->value;
    lx_host_monitor_unlock(mutex);
    lx_handle_release(&borrow);
    return NO_ERROR;
}
ULONG WinQuerySwitchHandle(ULONG window, ULONG process)
{
    TRACE_NATIVE("WinQuerySwitchHandle(%u, %u)", (uint)window, (uint)process);
    lx_host_once(&once, initialize);
    if (!process)
    {
        uint32 parent;
        lx_host_process_ids(&process, &parent);
    }
    ULONG result = 0;
    lx_host_monitor_lock(mutex);
    for (SwitchEntry *entry = entries; entry; entry = entry->next)
        if (entry->value.idProcess == process && (!window || entry->value.hwnd == window))
        {
            result = entry->handle;
            break;
        }
    lx_host_monitor_unlock(mutex);
    return result;
}
