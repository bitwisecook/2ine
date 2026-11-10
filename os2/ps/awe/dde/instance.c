/* Public domain. No rights reserved. Wholly AI-authored. */
/* DDE dde_instances retain callback registration and per-instance errors on their owning thread. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "emulator/handles.h"
#include "os2/ps/awe/dde/dde.h"

/* The DdeInitialize flags accepted: monitor flags, callback filters, client-only and
   filter-initiates commands, and the application class. */
static const ULONG DDE_INITIALIZE_FLAGS =
    WIN32_MF_MASK | WIN32_CBF_FAIL_ALLSVRXACTIONS | WIN32_CBF_SKIP_ALLNOTIFICATIONS |
    WIN32_APPCMD_CLIENTONLY | WIN32_APPCMD_FILTERINITS | WIN32_APPCLASS_MONITOR;

static void cleanup_dde_owned(ULONG id);

static LxHostOnce dde_once = LX_HOST_ONCE_INIT;

LxHostMonitor *dde_lock;

DdeInstance *dde_instances;

static void dde_initialize(void)
{
    dde_lock = lx_host_monitor_create();
    if (!dde_lock)
        abort();
}

static void destroy_instance(void *resource)
{
    DdeInstance *instance = resource;
    dde_withdraw_service(instance->handle, 0, TRUE);
    cleanup_dde_owned(instance->handle);
    lx_host_monitor_lock(dde_lock);
    for (DdeInstance **link = &dde_instances; *link; link = &(*link)->next)
        if (*link == instance)
        {
            *link = instance->next;
            break;
        }
    lx_host_monitor_unlock(dde_lock);
    while (instance->strings)
    {
        DdeString *string = instance->strings;
        instance->strings = string->next;
        for (ULONG i = 0; i < string->references; i++)
            GlobalDeleteAtom(string->atom);
        if (string->service)
            GlobalDeleteAtom(string->atom);
        free(string);
    }
    free(instance);
}

BOOL dde_borrow_instance(ULONG handle, LxHandleBorrow *borrow)
{
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE, borrow))
        return FALSE;
    if (((DdeInstance *)borrow->resource)->owner == lxHostThreadState())
        return TRUE;
    lx_handle_release(borrow);
    return FALSE;
}

ULONG DdeInitialize(PULONG id, ULONG callback, ULONG flags, ULONG reserved)
{
    TRACE_NATIVE("DdeInitialize(%p, %x, %x, %u)", id, (uint)callback, (uint)flags, (uint)reserved);
    if (!id || reserved || (flags & ~DDE_INITIALIZE_FLAGS) ||
        (flags & WIN32_APPCLASS_MASK) > WIN32_APPCLASS_MONITOR || !open32ValidProcedure(callback))
        return WIN32_DMLERR_INVALIDPARAMETER;
    /* Monitor callbacks are not provided. */
    if ((flags & WIN32_APPCLASS_MASK) == WIN32_APPCLASS_MONITOR)
        return WIN32_DMLERR_DLL_USAGE;
    if (*id)
    {
        LxHandleBorrow borrow;
        if (!dde_borrow_instance(*id, &borrow))
            return WIN32_DMLERR_INVALIDPARAMETER;
        DdeInstance *instance = borrow.resource;
        instance->callback = callback;
        instance->flags = flags;
        instance->error = 0;
        lx_handle_release(&borrow);
        return 0;
    }
    if (!open32Presentation())
        return WIN32_DMLERR_SYS_ERROR;
    DdeInstance *instance = calloc(1, sizeof *instance);
    if (!instance)
        return WIN32_DMLERR_LOW_MEMORY;
    lx_host_once(&dde_once, dde_initialize);
    instance->owner = lxHostThreadState();
    instance->thread = GetCurrentThreadId();
    instance->callback = callback;
    instance->flags = flags;
    instance->filter = TRUE;
    instance->handle = lx_handle_create(LX_HANDLE_OPEN32_DDE, instance, destroy_instance);
    if (!instance->handle)
    {
        free(instance);
        return WIN32_DMLERR_LOW_MEMORY;
    }
    lx_host_monitor_lock(dde_lock);
    instance->next = dde_instances;
    dde_instances = instance;
    lx_host_monitor_unlock(dde_lock);
    dde_start_listener();
    *id = instance->handle;
    return 0;
}

ULONG DdeGetLastError(ULONG id)
{
    LxHandleBorrow borrow;
    if (!dde_borrow_instance(id, &borrow))
        return WIN32_DMLERR_INVALIDPARAMETER;
    DdeInstance *instance = borrow.resource;
    ULONG error = instance->error;
    instance->error = 0;
    lx_handle_release(&borrow);
    return error;
}

BOOL DdeUninitialize(ULONG id)
{
    LxHandleBorrow borrow;
    if (!dde_borrow_instance(id, &borrow))
        return FALSE;
    BOOL result = lx_handle_close(id, LX_HANDLE_OPEN32_DDE);
    lx_handle_release(&borrow);
    return result;
}

void open32DdeCleanup(void)
{
    lx_host_once(&dde_once, dde_initialize);
    for (;;)
    {
        lx_host_monitor_lock(dde_lock);
        ULONG handle = dde_instances ? dde_instances->handle : 0;
        lx_host_monitor_unlock(dde_lock);
        if (!handle)
        {
            dde_stop_listener();
            return;
        }
        lx_handle_close(handle, LX_HANDLE_OPEN32_DDE);
    }
}

void open32DdeSetError(ULONG id, ULONG error)
{
    LxHandleBorrow borrow;
    if (dde_borrow_instance(id, &borrow))
    {
        ((DdeInstance *)borrow.resource)->error = error;
        lx_handle_release(&borrow);
    }
}

static void cleanup_dde_owned(ULONG id)
{
    lx_host_monitor_lock(dde_lock);
    for (DdeRequest *request = dde_requests; request; request = request->next)
        if (request->instance == id || request->client == id)
            request->abandoned = TRUE;
    lx_host_monitor_wake_all(dde_lock);
    lx_host_monitor_unlock(dde_lock);
    for (;;)
    {
        ULONG handle = 0;
        lx_host_monitor_lock(dde_lock);
        for (DdeConversation **link = &dde_conversations; *link; link = &(*link)->next)
            if ((*link)->client == id || (*link)->server == id)
            {
                DdeConversation *conversation = *link;
                handle = conversation->handle;
                *link = conversation->next;
                break;
            }
        lx_host_monitor_unlock(dde_lock);
        if (!handle)
            break;
        lx_handle_close(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION);
    }
    for (;;)
    {
        ULONG handle = 0;
        lx_host_monitor_lock(dde_lock);
        for (DdeData **link = &dde_data; *link; link = &(*link)->next)
            if ((*link)->owner == id)
            {
                DdeData *data = *link;
                handle = data->handle;
                *link = data->next;
                break;
            }
        lx_host_monitor_unlock(dde_lock);
        if (!handle)
            break;
        lx_handle_close(handle, LX_HANDLE_OPEN32_DDE_DATA);
    }
}
