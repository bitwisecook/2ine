/* Public domain. No rights reserved. Wholly AI-authored. */
/* Switch entries belong to this worker; no host desktop task list is modified. */
#include "os2libs/os2native.h"
#include "os2libs/pmshapi.h"
#include "os2libs/sesmgr_start.h"
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
    /* An untitled entry takes the title its session was started with. */
    if (!*entry->value.szSwtitle)
        lx_session_title(entry->value.szSwtitle, sizeof entry->value.szSwtitle);
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
    /* A title set after the entry was added still names this process's session. */
    char session_name[sizeof value->szSwtitle];
    uint32 process, parent;
    lx_host_process_ids(&process, &parent);
    lx_session_title(session_name, sizeof session_name);
    if (*session_name && value->idProcess == process)
        memcpy(value->szSwtitle, session_name, sizeof session_name);
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

ULONG WinQueryTaskSizePos(HANDLE hab, ULONG session, PSWP position)
{
    TRACE_NATIVE("WinQueryTaskSizePos(%u, %u, %p)", (uint)hab, (uint)session, position);
    if (!position)
        return ERROR_INVALID_PARAMETER;
    lx_host_once(&once, initialize);
    HWND window = 0;
    lx_host_monitor_lock(mutex);
    for (SwitchEntry *entry = entries; entry; entry = entry->next)
        if (entry->value.idSession == session)
        {
            window = entry->value.hwnd;
            break;
        }
    lx_host_monitor_unlock(mutex);
    if (!window)
        return ERROR_SMG_SESSION_NOT_FOUND;
    return WinQueryWindowPos(window, position) ? NO_ERROR : ERROR_SMG_NO_TARGET_WINDOW;
}

ULONG WinCreateSwitchEntry(HAB hab, PSWCNTRL value)
{
    /* Process and session identities have a 16-bit form in the switch list. */
    if (!hab || !value || value->idProcess > 0xffff || value->idSession > 0xffff)
        return 0;
    return WinAddSwitchEntry(value);
}

ULONG WinQueryTaskTitle(ULONG session, PSZ title, ULONG capacity)
{
    if (!title || !capacity)
        return ERROR_INVALID_PARAMETER;
    char session_name[64] = {0};
    if (!session)
        lx_session_title(session_name, sizeof session_name);
    if (*session_name)
    {
        snprintf(title, capacity, "%s", session_name);
        return NO_ERROR;
    }
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(mutex);
    ULONG result = ERROR_SMG_SESSION_NOT_FOUND;
    *title = 0;
    /* Session zero names the caller; a long title is truncated without error. */
    uint32 process = 0, parent;
    if (!session)
        lx_host_process_ids(&process, &parent);
    for (SwitchEntry *entry = entries; entry; entry = entry->next)
        if (session ? entry->value.idSession == session : entry->value.idProcess == process)
        {
            size_t length = strlen(entry->value.szSwtitle);
            size_t copied = length < capacity ? length : capacity - 1;
            memcpy(title, entry->value.szSwtitle, copied);
            title[copied] = 0;
            result = NO_ERROR;
            break;
        }
    lx_host_monitor_unlock(mutex);
    /* Before a Window List entry exists the caller's title is its program name. */
    const char *program = GLoaderState.pib.pib_pchcmd;
    if (result != NO_ERROR && !session && program && *program)
    {
        const char *name = program;
        for (const char *p = program; *p; p++)
            if (*p == '\\' || *p == '/')
                name = p + 1;
        size_t length = strlen(name), copied = length < capacity ? length : capacity - 1;
        memcpy(title, name, copied);
        title[copied] = 0;
        result = NO_ERROR;
    }
    return result;
}
