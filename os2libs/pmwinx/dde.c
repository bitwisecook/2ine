/* Public domain. No rights reserved. Wholly AI-authored. */
/* DDE instances retain callback registration and per-instance errors on their owning thread. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "guest/handles.h"
#include "hal/host_sync.h"
typedef struct DdeString
{
    struct DdeString *next;
    ULONG atom, references;
    BOOL service;
} DdeString;
typedef struct DdeInstance
{
    struct DdeInstance *next;
    void *owner;
    DdeString *strings;
    BOOL filter;
    ULONG handle, callback, flags, error;
} DdeInstance;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *lock;
static DdeInstance *instances;
static void initialize(void)
{
    lock = lx_host_monitor_create();
    if (!lock)
        abort();
}
static void destroy_instance(void *resource)
{
    DdeInstance *instance = resource;
    lx_host_monitor_lock(lock);
    for (DdeInstance **link = &instances; *link; link = &(*link)->next)
        if (*link == instance)
        {
            *link = instance->next;
            break;
        }
    lx_host_monitor_unlock(lock);
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
static BOOL borrow_instance(ULONG handle, LxHandleBorrow *borrow)
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
    if (!id || reserved || (flags & ~0xff3ff031u) || (flags & 15) > 1 ||
        !open32ValidProcedure(callback))
        return 0x4006; /* DMLERR_INVALIDPARAMETER */
    if ((flags & 15) == 1)
        return 0x4004; /* DMLERR_DLL_USAGE: monitor callbacks are not provided. */
    if (*id)
    {
        LxHandleBorrow borrow;
        if (!borrow_instance(*id, &borrow))
            return 0x4006;
        DdeInstance *instance = borrow.resource;
        instance->callback = callback;
        instance->flags = flags;
        instance->error = 0;
        lx_handle_release(&borrow);
        return 0;
    }
    if (!open32Presentation())
        return 0x400f; /* DMLERR_SYS_ERROR */
    DdeInstance *instance = calloc(1, sizeof *instance);
    if (!instance)
        return 0x4007;
    lx_host_once(&once, initialize);
    instance->owner = lxHostThreadState();
    instance->callback = callback;
    instance->flags = flags;
    instance->filter = TRUE;
    instance->handle = lx_handle_create(LX_HANDLE_OPEN32_DDE, instance, destroy_instance);
    if (!instance->handle)
    {
        free(instance);
        return 0x4007;
    }
    lx_host_monitor_lock(lock);
    instance->next = instances;
    instances = instance;
    lx_host_monitor_unlock(lock);
    *id = instance->handle;
    return 0;
}
ULONG DdeGetLastError(ULONG id)
{
    LxHandleBorrow borrow;
    if (!borrow_instance(id, &borrow))
        return 0x4006;
    DdeInstance *instance = borrow.resource;
    ULONG error = instance->error;
    instance->error = 0;
    lx_handle_release(&borrow);
    return error;
}
BOOL DdeUninitialize(ULONG id)
{
    LxHandleBorrow borrow;
    if (!borrow_instance(id, &borrow))
        return FALSE;
    BOOL result = lx_handle_close(id, LX_HANDLE_OPEN32_DDE);
    lx_handle_release(&borrow);
    return result;
}
void open32DdeCleanup(void)
{
    lx_host_once(&once, initialize);
    for (;;)
    {
        lx_host_monitor_lock(lock);
        ULONG handle = instances ? instances->handle : 0;
        lx_host_monitor_unlock(lock);
        if (!handle)
            return;
        lx_handle_close(handle, LX_HANDLE_OPEN32_DDE);
    }
}

static DdeString **find_string(DdeInstance *instance, ULONG atom)
{
    DdeString **link = &instance->strings;
    while (*link && (*link)->atom != atom)
        link = &(*link)->next;
    return link;
}
ULONG DdeCreateStringHandle(ULONG id, PSZ text, LONG codepage)
{
    TRACE_NATIVE("DdeCreateStringHandle(%u, '%s', %d)", (uint)id, text ? text : "", (int)codepage);
    LxHandleBorrow borrow;
    if (!borrow_instance(id, &borrow))
        return 0;
    DdeInstance *instance = borrow.resource;
    ULONG atom = 0;
    if (!text || strlen(text) > 255 || (codepage && codepage != 1004))
        instance->error = 0x4006;
    else if (*text)
    {
        atom = GlobalAddAtom(text);
        DdeString **link = find_string(instance, atom);
        DdeString *entry = *link;
        if (atom && !entry)
        {
            entry = calloc(1, sizeof *entry);
            if (entry)
            {
                entry->atom = atom;
                *link = entry;
            }
        }
        if (!atom || !entry || entry->references == UINT32_MAX)
        {
            if (atom)
                GlobalDeleteAtom(atom);
            atom = 0;
            instance->error = 0x4007;
        }
        else
            entry->references++;
    }
    lx_handle_release(&borrow);
    return atom;
}
BOOL DdeFreeStringHandle(ULONG id, ULONG atom)
{
    LxHandleBorrow borrow;
    if (!borrow_instance(id, &borrow))
        return FALSE;
    DdeInstance *instance = borrow.resource;
    DdeString **link = find_string(instance, atom);
    BOOL result = *link && (*link)->references;
    if (!result)
        instance->error = 0x4006;
    else
    {
        DdeString *entry = *link;
        GlobalDeleteAtom(entry->atom);
        if (!--entry->references && !entry->service)
        {
            *link = entry->next;
            free(entry);
        }
    }
    lx_handle_release(&borrow);
    return result;
}
BOOL DdeKeepStringHandle(ULONG id, ULONG atom)
{
    char name[256];
    if (!GlobalGetAtomName(atom, name, sizeof name))
    {
        LxHandleBorrow borrow;
        if (borrow_instance(id, &borrow))
        {
            ((DdeInstance *)borrow.resource)->error = 0x4006;
            lx_handle_release(&borrow);
        }
        return FALSE;
    }
    return DdeCreateStringHandle(id, name, 1004) == atom;
}
ULONG DdeQueryString(ULONG id, ULONG atom, PSZ output, ULONG capacity, LONG codepage)
{
    LxHandleBorrow borrow;
    if (!borrow_instance(id, &borrow))
        return 0;
    DdeInstance *instance = borrow.resource;
    char name[256];
    ULONG length = GlobalGetAtomName(atom, name, sizeof name), result = 0;
    if (!length || (codepage && codepage != 1004))
        instance->error = 0x4006;
    else if (!output)
        result = length;
    else if (capacity)
    {
        result = length < capacity ? length : capacity - 1;
        memcpy(output, name, result);
        output[result] = 0;
    }
    lx_handle_release(&borrow);
    return result;
}
LONG DdeCmpStringHandles(ULONG first, ULONG second)
{
    if (first == second)
        return 0;
    char left[256], right[256];
    if (!GlobalGetAtomName(first, left, sizeof left))
        return -1;
    if (!GlobalGetAtomName(second, right, sizeof right))
        return 1;
    int result = strcasecmp(left, right);
    return result < 0 ? -1 : result > 0 ? 1 : 0;
}

void open32DdeSetError(ULONG id, ULONG error)
{
    LxHandleBorrow borrow;
    if (borrow_instance(id, &borrow))
    {
        ((DdeInstance *)borrow.resource)->error = error;
        lx_handle_release(&borrow);
    }
}
ULONG DdeNameService(ULONG id, ULONG atom, ULONG reserved, ULONG command)
{
    TRACE_NATIVE("DdeNameService(%u, %u, %u, %u)", (uint)id, (uint)atom, (uint)reserved,
                 (uint)command);
    LxHandleBorrow borrow;
    if (!borrow_instance(id, &borrow))
        return 0;
    DdeInstance *instance = borrow.resource;
    ULONG result = 0;
    DdeString **link = find_string(instance, atom);
    if (reserved || (command != 1 && command != 2 && command != 4 && command != 8) ||
        (command == 1 && (!atom || !*link)) || (atom && !*link))
        instance->error = 0x4006;
    else if (command == 4 || command == 8)
    {
        instance->filter = command == 4;
        result = 1;
    }
    else if (command == 1 && (instance->flags & 0x10))
        instance->error = 0x4004; /* Client-only instances cannot register a service. */
    else
    {
        BOOL unsupported = FALSE;
        ULONG skip = command == 1 ? 0x80000 : 0x100000;
        lx_host_monitor_lock(lock);
        for (DdeInstance *peer = instances; peer; peer = peer->next)
            if (peer != instance && !(peer->flags & skip))
                unsupported = TRUE;
        lx_host_monitor_unlock(lock);
        if (unsupported)
            instance->error = 0x4004; /* Peer registration notifications require a DDE transport. */
        else if (command == 1)
        {
            DdeString *entry = *link;
            char name[256];
            if (entry->service)
                result = 1;
            else if (GlobalGetAtomName(atom, name, sizeof name) && GlobalAddAtom(name) == atom)
            {
                entry->service = TRUE;
                result = 1;
            }
            else
                instance->error = 0x4007;
        }
        else
        {
            for (DdeString **item = &instance->strings; *item;)
            {
                DdeString *entry = *item;
                if (entry->service && (!atom || atom == entry->atom))
                {
                    entry->service = FALSE;
                    GlobalDeleteAtom(entry->atom);
                    if (!entry->references)
                    {
                        *item = entry->next;
                        free(entry);
                        continue;
                    }
                }
                item = &entry->next;
            }
            result = 1;
        }
    }
    lx_handle_release(&borrow);
    return result;
}
