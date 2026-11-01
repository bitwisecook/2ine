/* Public domain. No rights reserved. Wholly AI-authored. */
/* Thread keyboard hooks retain callbacks while an in-flight chain is executing. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "hal/host_sync.h"
typedef struct Hook
{
    ULONG handle, procedure, thread;
    BOOL removed;
    LxHandleBorrow module;
    struct Hook *next;
} Hook;
typedef struct HookSnapshot
{
    LxHandleBorrow *entries;
    size_t count;
} HookSnapshot;
typedef struct HookCall
{
    HookSnapshot *snapshot;
    size_t next;
    struct HookCall *previous;
} HookCall;
static Hook *hooks;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *monitor;
static _Thread_local HookCall *current;
static void initialize(void)
{
    monitor = lx_host_monitor_create();
    if (!monitor)
        abort();
}
static void destroy_hook(void *data)
{
    Hook *hook = data;
    if (hook->module.resource)
    {
        if (GLoaderState.freeModule)
            GLoaderState.freeModule(hook->module.resource);
        lx_handle_release(&hook->module);
    }
    free(hook);
}
ULONG SetWindowsHookEx(LONG type, ULONG procedure, ULONG module, ULONG thread)
{
    TRACE_NATIVE("SetWindowsHookEx(%d, %X, %u, %u)", type, procedure, module, thread);
    if (type != 2 || thread != GetCurrentThreadId())
    {
        open32SetError(50);
        return 0;
    }
    if (!open32ValidProcedure(procedure))
    {
        open32SetError(87);
        return 0;
    }
    Hook *hook = calloc(1, sizeof *hook);
    if (!hook)
    {
        open32SetError(8);
        return 0;
    }
    if (module)
    {
        if (!lx_handle_borrow(module, LX_HANDLE_MODULE, &hook->module))
        {
            free(hook);
            open32SetError(6);
            return 0;
        }
        __atomic_add_fetch(&((LxModule *)hook->module.resource)->refcount, 1, __ATOMIC_ACQ_REL);
    }
    hook->procedure = procedure;
    hook->thread = thread;
    hook->handle = lx_handle_create(LX_HANDLE_OPEN32_HOOK, hook, destroy_hook);
    if (!hook->handle)
    {
        destroy_hook(hook);
        open32SetError(8);
        return 0;
    }
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    hook->next = hooks;
    hooks = hook;
    lx_host_monitor_unlock(monitor);
    return hook->handle;
}
BOOL UnhookWindowsHookEx(ULONG handle)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    Hook **link = &hooks;
    while (*link && (*link)->handle != handle)
        link = &(*link)->next;
    Hook *hook = *link;
    if (!hook)
    {
        lx_host_monitor_unlock(monitor);
        open32SetError(1404);
        return FALSE;
    }
    *link = hook->next;
    hook->removed = TRUE;
    lx_host_monitor_unlock(monitor);
    return lx_handle_close(handle, LX_HANDLE_OPEN32_HOOK);
}
static LONG invoke_hook(HookSnapshot *snapshot, size_t index, LONG code, ULONG key, ULONG parameter)
{
    while (index < snapshot->count && ((Hook *)snapshot->entries[index].resource)->removed)
        index++;
    if (index == snapshot->count)
        return 0;
    Hook *hook = snapshot->entries[index].resource;
    HookCall call = {snapshot, index + 1, current};
    current = &call;
    LONG result = (LONG)lx_execution_callback32_4(GLoaderState.execution, hook->procedure,
                                                  (ULONG)code, key, parameter, 0);
    current = call.previous;
    return result;
}
LONG CallNextHookEx(ULONG hook, LONG code, ULONG key, ULONG parameter)
{
    (void)hook;
    return current ? invoke_hook(current->snapshot, current->next, code, key, parameter) : 0;
}
LONG open32RunKeyboardHooks(LONG code, ULONG key, ULONG parameter)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    size_t count = 0;
    ULONG thread = GetCurrentThreadId();
    for (Hook *hook = hooks; hook; hook = hook->next)
        if (hook->thread == thread)
            count++;
    HookSnapshot snapshot = {calloc(count ? count : 1, sizeof(LxHandleBorrow)), 0};
    if (!snapshot.entries)
    {
        lx_host_monitor_unlock(monitor);
        open32SetError(8);
        return 0;
    }
    for (Hook *hook = hooks; hook; hook = hook->next)
        if (hook->thread == thread && lx_handle_borrow(hook->handle, LX_HANDLE_OPEN32_HOOK,
                                                       &snapshot.entries[snapshot.count]))
            snapshot.count++;
    lx_host_monitor_unlock(monitor);
    LONG result = invoke_hook(&snapshot, 0, code, key, parameter);
    for (size_t i = 0; i < snapshot.count; i++)
        lx_handle_release(&snapshot.entries[i]);
    free(snapshot.entries);
    return result;
}
void open32HooksCleanup(void)
{
    while (hooks)
        UnhookWindowsHookEx(hooks->handle);
}
