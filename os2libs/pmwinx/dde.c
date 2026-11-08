/* Public domain. No rights reserved. Wholly AI-authored. */
/* DDE instances retain callback registration and per-instance errors on their owning thread.
   Callbacks always run on that thread: requests from other threads are queued for it. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/batch_services.h"
#include "guest/handles.h"
#include "host/clock.h"
#include "host/mailbox.h"
#include "host/process.h"
#include "host/shared_memory.h"
#include "host/sync.h"
#include "host/thread.h"
#include <stdio.h>

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
    ULONG handle, callback, flags, error, thread;
} DdeInstance;

static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *lock;
static DdeInstance *instances;
static void cleanup_dde_owned(ULONG id);
static void dde_notify_peers(DdeInstance *instance, ULONG type, ULONG skip, ULONG atom);
static void publish_service(ULONG instance, ULONG atom);
static void withdraw_service(ULONG instance, ULONG atom, BOOL all);
static void start_listener(void);
static void stop_listener(void);

static void initialize(void)
{
    lock = lx_host_monitor_create();
    if (!lock)
        abort();
}

static void destroy_instance(void *resource)
{
    DdeInstance *instance = resource;
    withdraw_service(instance->handle, 0, TRUE);
    cleanup_dde_owned(instance->handle);
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
    instance->thread = GetCurrentThreadId();
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
    start_listener();
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
        {
            stop_listener();
            return;
        }
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
        if (command == 8) /* DNS_FILTEROFF: this server answers for any service. */
            publish_service(instance->handle, 0);
        else
            withdraw_service(instance->handle, 0, FALSE);
        result = 1;
    }
    else if (command == 1 && (instance->flags & 0x10))
        instance->error = 0x4004; /* Client-only instances cannot register a service. */
    else
    {
        if (command == 1)
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
        if (result && command == 1)
            publish_service(instance->handle, atom);
        else if (result)
            withdraw_service(instance->handle, atom, !atom);
        if (result)
            dde_notify_peers(instance, command == 1 ? 0x10A2 : 0x10D2,
                             command == 1 ? 0x80000 : 0x100000, atom);
    }
    lx_handle_release(&borrow);
    return result;
}

/* Data and conversations are typed resources. Guest callbacks use an x86
   argument thunk, keeping all eight callback words out of the host C ABI. */
typedef struct DdeData
{
    LxGuestMapping *mapping;
    ULONG owner, size, item, format, flags, accesses, handle;
    struct DdeData *next;
} DdeData;

/* A conversation with another process names only its local side, in client or server;
   remote_handle is the peer's conversation in remote_process. */
typedef struct DdeConversation
{
    ULONG handle, client, server, service, topic, user, item, format, type;
    ULONG remote_process, remote_handle;
    BOOL advise;
    struct DdeConversation *next;
} DdeConversation;

static DdeData *dde_data;
static DdeConversation *dde_conversations;
static LxGuestMapping *dde_callback_code;

static ULONG dde_callback(DdeInstance *instance, ULONG type, ULONG format, ULONG conversation,
                          ULONG first, ULONG second, ULONG data, ULONG one, ULONG two)
{
    if (!instance || instance->owner != lxHostThreadState() || !instance->callback)
        return 0;
    if (!dde_callback_code)
    {
        static const uint8 code[] = {0x8B, 0x44, 0x24, 0x04, 0xFF, 0x70, 0x1C, 0xFF, 0x70,
                                     0x18, 0xFF, 0x70, 0x14, 0xFF, 0x70, 0x10, 0xFF, 0x70,
                                     0x0C, 0xFF, 0x70, 0x08, 0xFF, 0x70, 0x04, 0xFF, 0x30,
                                     0xFF, 0x50, 0x20, 0x83, 0xC4, 0x20, 0xC3};
        dde_callback_code = lx_execution_map(GLoaderState.execution, 0, 4096, 3, 0);
        if (!dde_callback_code)
            return 0;
        memcpy(lx_execution_mapping_host(dde_callback_code), code, sizeof code);
        if (!lx_execution_protect(GLoaderState.execution, dde_callback_code,
                                  LX_GUEST_READ | LX_GUEST_EXECUTE))
        {
            lx_execution_unmap(GLoaderState.execution, dde_callback_code);
            dde_callback_code = NULL;
            return 0;
        }
    }
    LxGuestMapping *arguments = lx_execution_map(GLoaderState.execution, 0, 4096, 3, 0);
    if (!arguments)
        return 0;
    ULONG values[] = {type, format, conversation,      first, second, data,
                      one,  two,    instance->callback};
    for (unsigned i = 0; i < 9; i++)
        lx_guest_store_u32((uint8 *)lx_execution_mapping_host(arguments) + i * 4, values[i]);
    ULONG result = lx_execution_callback32_4(GLoaderState.execution,
                                             lx_execution_mapping_address(dde_callback_code),
                                             lx_execution_mapping_address(arguments), 0, 0, 0);
    lx_execution_unmap(GLoaderState.execution, arguments);
    return result;
}

/* One unit of work for an instance's owning thread. A synchronous sender waits for done;
   an asynchronous one is answered with XTYP_XACT_COMPLETE queued to the client thread. */
typedef struct DdeRequest
{
    struct DdeRequest *next;
    ULONG thread, instance, type, format, conversation, first, second, data, one, two;
    ULONG result, transaction, client, release_data, user;
    ULONG remote_process, remote_sequence, remote_peer, payload_size;
    uint8 *payload;
    BOOL asynchronous, waiting, done, abandoned, connect, forward;
} DdeRequest;

static DdeRequest *dde_requests;
static _Thread_local DdeRequest *dde_active; /* The request whose callback is running here. */
static ULONG next_transaction;

static BOOL borrow_any(ULONG handle, LxHandleBorrow *borrow)
{
    return lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE, borrow);
}

static void queue_request(DdeRequest *request)
{
    DdeRequest **link = &dde_requests;
    while (*link)
        link = &(*link)->next;
    request->next = NULL;
    *link = request;
}

static void forget_request(DdeRequest *request)
{
    for (DdeRequest **link = &dde_requests; *link; link = &(*link)->next)
        if (*link == request)
        {
            *link = request->next;
            break;
        }
}

static ULONG run_callback(const DdeRequest *request)
{
    LxHandleBorrow target = {0};
    if (!borrow_instance(request->instance, &target))
        return 0;
    ULONG result =
        dde_callback(target.resource, request->type, request->format, request->conversation,
                     request->first, request->second, request->data, request->one, request->two);
    lx_handle_release(&target);
    return result;
}

static void complete_transaction(DdeRequest *request);
static void serve_remote(DdeRequest *request);
static ULONG forward_transaction(const DdeRequest *request);

void open32DdeDeliver(void)
{
    if (!lock)
        return;
    ULONG thread = GetCurrentThreadId();
    for (;;)
    {
        lx_host_monitor_lock(lock);
        DdeRequest *request = dde_requests;
        while (request && (request->thread != thread || request->done))
            request = request->next;
        if (request)
            forget_request(request);
        lx_host_monitor_unlock(lock);
        if (!request)
            return;
        if (request->remote_process && !request->abandoned)
        {
            serve_remote(request);
            free(request->payload);
            free(request);
            continue;
        }
        if (request->abandoned)
        {
            lx_host_monitor_lock(lock);
            BOOL waited = request->waiting;
            request->done = TRUE;
            lx_host_monitor_wake_all(lock);
            lx_host_monitor_unlock(lock);
            free(request->payload);
            request->payload = NULL;
            if (!waited)
                free(request);
            continue;
        }
        DdeRequest *outer = dde_active;
        dde_active = request;
        ULONG result = request->forward ? forward_transaction(request) : run_callback(request);
        dde_active = outer;
        lx_host_monitor_lock(lock);
        request->result = result;
        request->done = TRUE;
        BOOL waiting = request->waiting;
        lx_host_monitor_wake_all(lock);
        lx_host_monitor_unlock(lock);
        if (request->asynchronous)
            complete_transaction(request);
        if (!waiting)
            free(request);
    }
}

/* Runs a callback on the instance's thread, waiting up to timeout milliseconds. Requests
   addressed to this thread are served meanwhile, so two threads cannot block each other. */
static BOOL dde_invoke(ULONG instance, ULONG timeout, ULONG type, ULONG format, ULONG conversation,
                       ULONG first, ULONG second, ULONG data, ULONG one, ULONG two, ULONG *result)
{
    LxHandleBorrow target = {0};
    if (!borrow_any(instance, &target))
        return FALSE;
    DdeInstance *peer = target.resource;
    ULONG thread = peer->thread;
    BOOL local = peer->owner == lxHostThreadState();
    if (local)
    {
        *result = dde_callback(peer, type, format, conversation, first, second, data, one, two);
        lx_handle_release(&target);
        return TRUE;
    }
    lx_handle_release(&target);
    DdeRequest *request = calloc(1, sizeof *request);
    if (!request)
        return FALSE;
    *request = (DdeRequest){.thread = thread,
                            .instance = instance,
                            .type = type,
                            .format = format,
                            .conversation = conversation,
                            .first = first,
                            .second = second,
                            .data = data,
                            .one = one,
                            .two = two,
                            .waiting = TRUE};
    lx_host_monitor_lock(lock);
    queue_request(request);
    lx_host_monitor_unlock(lock);
    if (!open32PostThreadPacket(thread, OPEN32_DDE_PACKET))
    {
        lx_host_monitor_lock(lock);
        forget_request(request);
        lx_host_monitor_unlock(lock);
        free(request);
        return FALSE;
    }
    uint64 deadline = lx_host_monotonic_us() + (uint64)timeout * 1000;
    BOOL done;
    for (;;)
    {
        open32DdeDeliver();
        lx_host_monitor_lock(lock);
        done = request->done;
        if (!done && lx_host_monotonic_us() < deadline)
            lx_host_monitor_wait(lock, 20);
        done = request->done;
        BOOL expired = !done && lx_host_monotonic_us() >= deadline;
        if (expired)
        {
            /* The owner frees a request it finds abandoned. */
            request->abandoned = TRUE;
            request->waiting = FALSE;
        }
        lx_host_monitor_unlock(lock);
        if (done || expired)
            break;
    }
    if (done)
    {
        *result = request->result;
        free(request);
    }
    return done;
}

static void notify_processes(ULONG type, ULONG skip, ULONG atom);

static void dde_notify_peers(DdeInstance *instance, ULONG type, ULONG skip, ULONG atom)
{
    notify_processes(type, skip, atom);
    ULONG *threads = NULL;
    size_t count = 0;
    lx_host_monitor_lock(lock);
    for (DdeInstance *peer = instances; peer; peer = peer->next)
        if (peer != instance && !(peer->flags & skip))
        {
            DdeRequest *request = calloc(1, sizeof *request);
            ULONG *grown = request ? realloc(threads, (count + 1) * sizeof *grown) : NULL;
            if (!grown)
            {
                free(request);
                break;
            }
            threads = grown;
            threads[count++] = peer->thread;
            request->thread = peer->thread;
            request->instance = peer->handle;
            request->type = type;
            request->first = atom;
            queue_request(request);
        }
    lx_host_monitor_unlock(lock);
    for (size_t i = 0; i < count; i++)
        open32PostThreadPacket(threads[i], OPEN32_DDE_PACKET);
    free(threads);
}

static void destroy_dde_data(void *resource)
{
    DdeData *data = resource;
    lx_host_monitor_lock(lock);
    DdeData **link = &dde_data;
    while (*link && *link != data)
        link = &(*link)->next;
    if (*link)
        *link = data->next;
    lx_host_monitor_unlock(lock);
    if (data->mapping)
        lx_execution_unmap(GLoaderState.execution, data->mapping);
    free(data);
}

ULONG DdeCreateDataHandle(ULONG id, PVOID input, ULONG count, ULONG offset, ULONG item,
                          ULONG format, ULONG flags)
{
    LxHandleBorrow instance = {0};
    if (!borrow_instance(id, &instance))
        return 0;
    DdeData *data = NULL;
    ULONG handle = 0;
    if ((flags & ~1u) || offset > 16u * 1024u * 1024u || count > 16u * 1024u * 1024u - offset)
    {
        ((DdeInstance *)instance.resource)->error = 0x4006;
        goto data_done;
    }
    data = calloc(1, sizeof *data);
    if (data)
        data->mapping = lx_execution_map(GLoaderState.execution, 0,
                                         (((size_t)offset + count + 4095) & ~(size_t)4095)
                                             ? (((size_t)offset + count + 4095) & ~(size_t)4095)
                                             : 4096,
                                         3, 0);
    if (!data || !data->mapping)
    {
        if (data)
            free(data);
        ((DdeInstance *)instance.resource)->error = 0x4007;
        goto data_done;
    }
    data->owner = id;
    data->size = offset + count;
    data->item = item;
    data->format = format;
    data->flags = flags;
    if (input && count)
        memcpy((uint8 *)lx_execution_mapping_host(data->mapping) + offset, input, count);
    handle = lx_handle_create(LX_HANDLE_OPEN32_DDE_DATA, data, destroy_dde_data);
    if (!handle)
    {
        lx_execution_unmap(GLoaderState.execution, data->mapping);
        free(data);
        ((DdeInstance *)instance.resource)->error = 0x4007;
        goto data_done;
    }
    data->handle = handle;
    lx_host_monitor_lock(lock);
    data->next = dde_data;
    dde_data = data;
    lx_host_monitor_unlock(lock);
data_done:
    lx_handle_release(&instance);
    return handle;
}

ULONG DdeAccessData(ULONG handle, PVOID size)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return 0;
    DdeData *data = borrow.resource;
    data->accesses++;
    if (size)
        lx_guest_store_u32(size, data->size);
    ULONG address = lx_execution_mapping_address(data->mapping);
    lx_handle_release(&borrow);
    return address;
}

BOOL DdeUnaccessData(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return FALSE;
    DdeData *data = borrow.resource;
    BOOL valid = data->accesses != 0;
    if (valid)
        data->accesses--;
    lx_handle_release(&borrow);
    return valid;
}

BOOL DdeFreeDataHandle(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return FALSE;
    DdeData *data = borrow.resource;
    BOOL valid = !data->accesses;
    if (valid)
        valid = lx_handle_close(handle, LX_HANDLE_OPEN32_DDE_DATA);
    else
        open32DdeSetError(data->owner, 0x4004);
    lx_handle_release(&borrow);
    return valid;
}

ULONG DdeAddData(ULONG handle, PVOID input, ULONG count, ULONG offset)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return 0;
    DdeData *data = borrow.resource;
    ULONG result = 0;
    if (data->accesses || (count && !input) || offset > 16u * 1024u * 1024u ||
        count > 16u * 1024u * 1024u - offset)
    {
        open32DdeSetError(data->owner, 0x4006);
        goto add_done;
    }
    ULONG required = count + offset;
    if (required > lx_execution_mapping_size(data->mapping))
    {
        LxGuestMapping *mapping = lx_execution_map(GLoaderState.execution, 0,
                                                   ((size_t)required + 4095) & ~(size_t)4095, 3, 0);
        if (!mapping)
        {
            open32DdeSetError(data->owner, 0x4007);
            goto add_done;
        }
        memcpy(lx_execution_mapping_host(mapping), lx_execution_mapping_host(data->mapping),
               data->size);
        lx_execution_unmap(GLoaderState.execution, data->mapping);
        data->mapping = mapping;
    }
    if (count)
        memcpy((uint8 *)lx_execution_mapping_host(data->mapping) + offset, input, count);
    if (required > data->size)
        data->size = required;
    result = handle;
add_done:
    lx_handle_release(&borrow);
    return result;
}

ULONG DdeGetData(ULONG handle, PVOID output, ULONG capacity, ULONG offset)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return 0;
    DdeData *data = borrow.resource;
    ULONG count = offset > data->size ? 0 : data->size - offset;
    if (output)
    {
        if (count > capacity)
            count = capacity;
        if (count)
            memcpy(output, (uint8 *)lx_execution_mapping_host(data->mapping) + offset, count);
    }
    lx_handle_release(&borrow);
    return count;
}

static void destroy_conversation(void *resource)
{
    DdeConversation *conversation = resource;
    lx_host_monitor_lock(lock);
    DdeConversation **link = &dde_conversations;
    while (*link && *link != conversation)
        link = &(*link)->next;
    if (*link)
        *link = conversation->next;
    lx_host_monitor_unlock(lock);
    free(conversation);
}

/* Other processes of the ball. The directory lists who serves what, each process reads a
   named mailbox on a host thread, and callbacks still run on the instance's owning thread.
   Data travels by value in the record, so data handles stay local to each process. */
enum
{
    REMOTE_CALL = 1,
    REMOTE_REPLY,
    REMOTE_CONNECT,
    REMOTE_NOTIFY,
    REMOTE_LIMIT = 4 * 1024 * 1024 - 256,
    SERVICE_LIMIT = 256,
    REMOTE_TIMEOUT = 10000
};

typedef struct RemoteRecord
{
    uint32 kind, sequence, process, conversation, peer, type, format, first, second, one, two;
    uint32 result, size;
} RemoteRecord;

typedef struct ServiceEntry
{
    uint32 process, instance, atom;
} ServiceEntry;

typedef struct ServiceDirectory
{
    uint32 lock;
    ServiceEntry entries[SERVICE_LIMIT];
} ServiceDirectory;

typedef struct RemoteReply
{
    struct RemoteReply *next;
    RemoteRecord record;
    uint8 *data;
} RemoteReply;

static LxHostOnce directory_once = LX_HOST_ONCE_INIT;
static ServiceDirectory *directory;
static uint32 own_process;
static LxHostMailbox *remote_inbox;
static LxHostThread *remote_thread;
static RemoteReply *remote_replies;
static uint32 remote_sequence, remote_stop;

static void mailbox_name(char name[32], uint32 process)
{
    snprintf(name, 32, "dde-%u", (unsigned)process);
}

static void initialize_directory(void)
{
    uint32 parent;
    lx_host_process_ids(&own_process, &parent);
    uint64 hash = 1469598103934665603ULL;
    const char *scope = getenv("TWOINE_BALL");
    if (!scope)
        scope = getenv("TWOINE_IPC_DIR");
    for (const char *p = scope; p && *p; p++)
        hash = (hash ^ (unsigned char)*p) * 1099511628211ULL;
    char name[40];
    snprintf(name, sizeof name, "/2ine-dde1-%016llx", (unsigned long long)hash);
    LxHostShared *shared = NULL;
    LxHostSharedResult opened = lx_host_shared_open(name, sizeof *directory, &shared);
    if (opened == LX_HOST_SHARED_EXISTS)
        opened = lx_host_shared_open(name, 0, &shared);
    if (opened == LX_HOST_SHARED_OK && lx_host_shared_size(shared) == sizeof *directory)
        directory = lx_host_shared_map(shared, NULL, 0);
}

static BOOL lock_directory(void)
{
    lx_host_once(&directory_once, initialize_directory);
    if (!directory)
        return FALSE;
    for (unsigned attempt = 0;; attempt++)
    {
        uint32 expected = 0;
        if (__atomic_compare_exchange_n(&directory->lock, &expected, 1, 0, __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED))
            return TRUE;
        if (attempt == 2000) /* The holder died; the table itself stays consistent. */
            return TRUE;
        lx_host_sleep_ms(1);
    }
}

static void unlock_directory(void)
{
    __atomic_store_n(&directory->lock, 0, __ATOMIC_RELEASE);
}

static BOOL process_alive(uint32 process)
{
    char name[32];
    mailbox_name(name, process);
    LxHostMailbox *mailbox;
    uint32 owner;
    if (lx_host_mailbox_open(getenv("TWOINE_IPC_DIR"), name, 0, 0, &mailbox, &owner))
        return FALSE;
    lx_host_mailbox_close(mailbox);
    return TRUE;
}

static void publish_service(ULONG instance, ULONG atom)
{
    if (!lock_directory())
        return;
    ServiceEntry *free_entry = NULL;
    for (unsigned i = 0; i < SERVICE_LIMIT; i++)
    {
        ServiceEntry *entry = directory->entries + i;
        if (!entry->process)
            free_entry = free_entry ? free_entry : entry;
        else if (entry->process == own_process && entry->instance == instance &&
                 entry->atom == atom)
            free_entry = NULL, i = SERVICE_LIMIT;
    }
    if (free_entry)
        *free_entry = (ServiceEntry){own_process, instance, atom};
    unlock_directory();
}

static void withdraw_service(ULONG instance, ULONG atom, BOOL all)
{
    if (!lock_directory())
        return;
    for (unsigned i = 0; i < SERVICE_LIMIT; i++)
    {
        ServiceEntry *entry = directory->entries + i;
        if (entry->process == own_process && entry->instance == instance &&
            (all || entry->atom == atom))
            *entry = (ServiceEntry){0};
    }
    unlock_directory();
}

static BOOL remote_send(uint32 process, const RemoteRecord *record, const void *data)
{
    char name[32];
    mailbox_name(name, process);
    LxHostMailbox *mailbox;
    uint32 owner;
    size_t size = sizeof *record + record->size;
    uint8 *wire = size <= REMOTE_LIMIT ? malloc(size) : NULL;
    if (!wire)
        return FALSE;
    memcpy(wire, record, sizeof *record);
    if (record->size)
        memcpy(wire + sizeof *record, data, record->size);
    BOOL sent = !lx_host_mailbox_open(getenv("TWOINE_IPC_DIR"), name, 0, 0, &mailbox, &owner);
    if (sent)
    {
        sent = !lx_host_mailbox_send(mailbox, wire, size, 0);
        lx_host_mailbox_close(mailbox);
    }
    free(wire);
    return sent;
}

static void notify_processes(ULONG type, ULONG skip, ULONG atom)
{
    uint32 processes[SERVICE_LIMIT];
    unsigned count = 0;
    if (!lock_directory())
        return;
    for (unsigned i = 0; i < SERVICE_LIMIT; i++)
    {
        uint32 process = directory->entries[i].process;
        unsigned known = 0;
        while (known < count && processes[known] != process)
            known++;
        if (process && process != own_process && known == count)
            processes[count++] = process;
    }
    unlock_directory();
    RemoteRecord record = {.kind = REMOTE_NOTIFY, .type = type, .first = atom, .one = skip};
    for (unsigned i = 0; i < count; i++)
        remote_send(processes[i], &record, NULL);
}

/* Waits for the reply while serving this thread's own queued work. */
static RemoteReply *remote_wait(uint32 sequence, ULONG timeout)
{
    uint64 deadline = lx_host_monotonic_us() + (uint64)timeout * 1000;
    for (;;)
    {
        open32DdeDeliver();
        lx_host_monitor_lock(lock);
        for (RemoteReply **link = &remote_replies; *link; link = &(*link)->next)
            if ((*link)->record.sequence == sequence)
            {
                RemoteReply *reply = *link;
                *link = reply->next;
                lx_host_monitor_unlock(lock);
                return reply;
            }
        if (lx_host_monotonic_us() >= deadline)
        {
            lx_host_monitor_unlock(lock);
            return NULL;
        }
        lx_host_monitor_wait(lock, 20);
        lx_host_monitor_unlock(lock);
    }
}

static void free_reply(RemoteReply *reply)
{
    free(reply->data);
    free(reply);
}

static uint8 *copy_data(ULONG handle, uint32 *size)
{
    LxHandleBorrow borrow = {0};
    *size = 0;
    if (!handle || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_DATA, &borrow))
        return NULL;
    DdeData *data = borrow.resource;
    uint8 *bytes = data->size ? malloc(data->size) : NULL;
    if (bytes)
    {
        memcpy(bytes, lx_execution_mapping_host(data->mapping), data->size);
        *size = data->size;
    }
    lx_handle_release(&borrow);
    return bytes;
}

static BOOL remote_call(DdeConversation *conversation, ULONG timeout, ULONG type, ULONG format,
                        ULONG first, ULONG second, ULONG data, ULONG one, ULONG two, ULONG *result)
{
    uint32 size;
    uint8 *bytes = copy_data(data, &size);
    uint32 sequence = __atomic_add_fetch(&remote_sequence, 1, __ATOMIC_RELAXED);
    RemoteRecord record = {.kind = REMOTE_CALL,
                           .sequence = sequence,
                           .process = own_process,
                           .conversation = conversation->remote_handle,
                           .type = type,
                           .format = format,
                           .first = first,
                           .second = second,
                           .one = one,
                           .two = two,
                           .size = size};
    BOOL sent = remote_send(conversation->remote_process, &record, bytes);
    free(bytes);
    RemoteReply *reply = sent ? remote_wait(sequence, timeout) : NULL;
    if (!reply)
        return FALSE;
    *result = reply->record.result;
    if (reply->record.size)
        *result =
            DdeCreateDataHandle(conversation->client ? conversation->client : conversation->server,
                                reply->data, reply->record.size, 0, second, format, 0);
    free_reply(reply);
    return TRUE;
}

/* Calls the far side of a conversation, in this process or another. */
static BOOL conversation_invoke(DdeConversation *conversation, ULONG peer, ULONG timeout,
                                ULONG type, ULONG format, ULONG first, ULONG second, ULONG data,
                                ULONG one, ULONG two, ULONG *result)
{
    if (conversation->remote_process)
        return remote_call(conversation, timeout, type, format, first, second, data, one, two,
                           result);
    return dde_invoke(peer, timeout, type, format, conversation->handle, first, second, data, one,
                      two, result);
}

static ULONG forward_transaction(const DdeRequest *request)
{
    LxHandleBorrow borrow = {0};
    ULONG result = 0;
    if (lx_handle_borrow(request->conversation, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
    {
        remote_call(borrow.resource, REMOTE_TIMEOUT, request->type, request->format, request->first,
                    request->second, request->data, 0, 0, &result);
        lx_handle_release(&borrow);
    }
    return result;
}

static ULONG make_conversation(ULONG client, ULONG server, ULONG service, ULONG topic,
                               ULONG process, ULONG peer)
{
    DdeConversation *conversation = calloc(1, sizeof *conversation);
    if (!conversation)
        return 0;
    conversation->client = client;
    conversation->server = server;
    conversation->service = service;
    conversation->topic = topic;
    conversation->remote_process = process;
    conversation->remote_handle = peer;
    ULONG handle =
        lx_handle_create(LX_HANDLE_OPEN32_DDE_CONVERSATION, conversation, destroy_conversation);
    if (!handle)
    {
        free(conversation);
        return 0;
    }
    conversation->handle = handle;
    lx_host_monitor_lock(lock);
    conversation->next = dde_conversations;
    dde_conversations = conversation;
    lx_host_monitor_unlock(lock);
    return handle;
}

/* Runs on the owning thread of the local instance named by the request. */
static void serve_remote(DdeRequest *request)
{
    RemoteRecord reply = {
        .kind = REMOTE_REPLY, .sequence = request->remote_sequence, .process = own_process};
    uint8 *bytes = NULL;
    if (request->connect)
    {
        ULONG accepted = run_callback(request);
        if (accepted)
        {
            ULONG handle = make_conversation(0, request->instance, request->second, request->first,
                                             request->remote_process, request->remote_peer);
            reply.peer = handle;
            reply.result = handle != 0;
            request->type = 0x8072; /* XTYP_CONNECT_CONFIRM */
            request->conversation = handle;
            if (handle)
                run_callback(request);
        }
    }
    else
    {
        if (request->payload_size)
            request->data =
                DdeCreateDataHandle(request->instance, request->payload, request->payload_size, 0,
                                    request->second, request->format, 0);
        ULONG result = run_callback(request);
        LxHandleBorrow borrow = {0};
        if (lx_handle_borrow(request->conversation, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
        {
            DdeConversation *conversation = borrow.resource;
            if ((request->type & ~0xCu) == 0x1030)
            {
                conversation->item = request->second;
                conversation->format = request->format;
            }
            if ((request->type & ~0xCu) == 0x1030 && result)
                conversation->advise = TRUE;
            if (request->type == 0x8040)
                conversation->advise = FALSE;
            lx_handle_release(&borrow);
        }
        reply.result = result;
        if (request->type == 0x20B0 && result)
        {
            bytes = copy_data(result, &reply.size);
            DdeFreeDataHandle(result);
        }
        if (request->data)
            DdeFreeDataHandle(request->data);
        if (request->type == 0x80C2)
            lx_handle_close(request->conversation, LX_HANDLE_OPEN32_DDE_CONVERSATION);
    }
    remote_send(request->remote_process, &reply, bytes);
    free(bytes);
}

static void queue_remote(DdeRequest *request)
{
    lx_host_monitor_lock(lock);
    queue_request(request);
    lx_host_monitor_unlock(lock);
    open32PostThreadPacket(request->thread, OPEN32_DDE_PACKET);
}

static void receive_call(const RemoteRecord *record, const uint8 *data)
{
    LxHandleBorrow borrow = {0}, owner = {0};
    if (!lx_handle_borrow(record->conversation, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
        return;
    DdeConversation *conversation = borrow.resource;
    ULONG instance = conversation->client ? conversation->client : conversation->server;
    DdeRequest *request = calloc(1, sizeof *request);
    if (request && borrow_any(instance, &owner))
    {
        *request = (DdeRequest){.thread = ((DdeInstance *)owner.resource)->thread,
                                .instance = instance,
                                .type = record->type,
                                .format = record->format,
                                .conversation = record->conversation,
                                .first = record->first,
                                .second = record->second,
                                .one = record->one,
                                .two = record->two,
                                .remote_process = record->process,
                                .remote_sequence = record->sequence,
                                .payload_size = record->size};
        lx_handle_release(&owner);
        request->payload = record->size ? malloc(record->size) : NULL;
        if (request->payload)
            memcpy(request->payload, data, record->size);
        if (record->size && !request->payload)
        {
            free(request);
            request = NULL;
        }
        if (request)
            queue_remote(request);
    }
    else
        free(request);
    lx_handle_release(&borrow);
}

static void receive_connect(const RemoteRecord *record)
{
    LxHandleBorrow owner = {0};
    DdeRequest *request = calloc(1, sizeof *request);
    if (!request || !borrow_any(record->peer, &owner))
    {
        free(request);
        RemoteRecord refusal = {
            .kind = REMOTE_REPLY, .sequence = record->sequence, .process = own_process};
        remote_send(record->process, &refusal, NULL);
        return;
    }
    *request = (DdeRequest){.thread = ((DdeInstance *)owner.resource)->thread,
                            .instance = record->peer,
                            .type = 0x1062, /* XTYP_CONNECT */
                            .first = record->first,
                            .second = record->second,
                            .remote_process = record->process,
                            .remote_sequence = record->sequence,
                            .remote_peer = record->conversation,
                            .connect = TRUE};
    lx_handle_release(&owner);
    queue_remote(request);
}

static void receive_notify(const RemoteRecord *record)
{
    lx_host_monitor_lock(lock);
    for (DdeInstance *peer = instances; peer; peer = peer->next)
    {
        DdeRequest *request = peer->flags & record->one ? NULL : calloc(1, sizeof *request);
        if (!request)
            continue;
        request->thread = peer->thread;
        request->instance = peer->handle;
        request->type = record->type;
        request->first = record->first;
        queue_request(request);
    }
    ULONG threads[64];
    unsigned count = 0;
    for (DdeInstance *peer = instances; peer && count < 64; peer = peer->next)
        threads[count++] = peer->thread;
    lx_host_monitor_unlock(lock);
    for (unsigned i = 0; i < count; i++)
        open32PostThreadPacket(threads[i], OPEN32_DDE_PACKET);
}

static void *remote_listener(void *argument)
{
    (void)argument;
    uint8 *wire = malloc(REMOTE_LIMIT);
    while (wire && !__atomic_load_n(&remote_stop, __ATOMIC_ACQUIRE))
    {
        size_t size = 0;
        unsigned priority;
        uint32 token = 0;
        if (lx_host_mailbox_receive(remote_inbox, wire, REMOTE_LIMIT, &size, &priority, &token, 0))
        {
            lx_host_sleep_ms(2);
            continue;
        }
        RemoteRecord record;
        if (size < sizeof record)
            continue;
        memcpy(&record, wire, sizeof record);
        const uint8 *data = wire + sizeof record;
        if (size != sizeof record + record.size)
            continue;
        if (record.kind == REMOTE_CALL)
            receive_call(&record, data);
        else if (record.kind == REMOTE_CONNECT)
            receive_connect(&record);
        else if (record.kind == REMOTE_NOTIFY)
            receive_notify(&record);
        else if (record.kind == REMOTE_REPLY)
        {
            RemoteReply *reply = calloc(1, sizeof *reply);
            if (!reply)
                continue;
            reply->record = record;
            reply->data = record.size ? malloc(record.size) : NULL;
            if (reply->data)
                memcpy(reply->data, data, record.size);
            lx_host_monitor_lock(lock);
            reply->next = remote_replies;
            remote_replies = reply;
            lx_host_monitor_wake_all(lock);
            lx_host_monitor_unlock(lock);
        }
    }
    free(wire);
    return NULL;
}

static void start_listener(void)
{
    lx_host_once(&directory_once, initialize_directory);
    lx_host_monitor_lock(lock);
    BOOL started = remote_inbox != NULL;
    lx_host_monitor_unlock(lock);
    if (started)
        return;
    char name[32];
    uint32 owner;
    mailbox_name(name, own_process);
    if (lx_host_mailbox_open(getenv("TWOINE_IPC_DIR"), name, 1, 0, &remote_inbox, &owner))
    {
        remote_inbox = NULL;
        return;
    }
    __atomic_store_n(&remote_stop, 0, __ATOMIC_RELEASE);
    remote_thread = lx_host_thread_create(remote_listener, NULL, NULL);
}

static void stop_listener(void)
{
    if (remote_thread)
    {
        __atomic_store_n(&remote_stop, 1, __ATOMIC_RELEASE);
        lx_host_thread_join(remote_thread, NULL);
        remote_thread = NULL;
    }
    if (remote_inbox)
    {
        lx_host_mailbox_close(remote_inbox);
        remote_inbox = NULL;
    }
}

/* The first live server in the directory that serves the name takes the conversation. */
static ULONG remote_connect(ULONG id, ULONG service, ULONG topic)
{
    ServiceEntry found = {0};
    if (!lock_directory())
        return 0;
    ServiceEntry snapshot[SERVICE_LIMIT];
    memcpy(snapshot, directory->entries, sizeof snapshot);
    unlock_directory();
    for (unsigned i = 0; i < SERVICE_LIMIT && !found.process; i++)
    {
        const ServiceEntry *entry = snapshot + i;
        if (entry->process && entry->process != own_process &&
            (!service || !entry->atom || entry->atom == service) && process_alive(entry->process))
            found = *entry;
    }
    if (!found.process)
        return 0;
    ULONG handle = make_conversation(id, 0, service, topic, found.process, 0);
    if (!handle)
        return 0;
    uint32 sequence = __atomic_add_fetch(&remote_sequence, 1, __ATOMIC_RELAXED);
    RemoteRecord record = {.kind = REMOTE_CONNECT,
                           .sequence = sequence,
                           .process = own_process,
                           .conversation = handle,
                           .peer = found.instance,
                           .first = topic,
                           .second = service};
    RemoteReply *reply =
        remote_send(found.process, &record, NULL) ? remote_wait(sequence, REMOTE_TIMEOUT) : NULL;
    BOOL accepted = reply && reply->record.result;
    LxHandleBorrow borrow = {0};
    if (accepted && lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
    {
        ((DdeConversation *)borrow.resource)->remote_handle = reply->record.peer;
        lx_handle_release(&borrow);
    }
    if (reply)
        free_reply(reply);
    if (accepted)
        return handle;
    lx_handle_close(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION);
    return 0;
}

ULONG DdeConnect(ULONG id, ULONG service, ULONG topic, PVOID context)
{
    LxHandleBorrow client = {0};
    if (!borrow_instance(id, &client))
        return 0;
    DdeInstance *instance = client.resource;
    ULONG server_handle = 0;
    if (context && lx_guest_load_u32(context) != 40)
    {
        instance->error = 0x4006;
        lx_handle_release(&client);
        return 0;
    }
    lx_host_monitor_lock(lock);
    for (DdeInstance *peer = instances; peer; peer = peer->next)
    {
        if (peer == instance || (peer->flags & 0x10))
            continue;
        BOOL match = !peer->filter;
        for (DdeString *string = peer->strings; string; string = string->next)
            if (string->service && (!service || service == string->atom))
            {
                match = TRUE;
                break;
            }
        if (match)
        {
            server_handle = peer->handle;
            break;
        }
    }
    lx_host_monitor_unlock(lock);
    ULONG handle = 0, accepted = 0;
    if (!server_handle)
    {
        handle = remote_connect(id, service, topic);
        if (!handle)
            instance->error = 0x400A;
        goto connect_done;
    }
    if (!dde_invoke(server_handle, 10000, 0x1062, 0, 0, topic, service, 0, 0, id == server_handle,
                    &accepted) ||
        !accepted)
    {
        instance->error = 0x400A;
        goto connect_done;
    }
    DdeConversation *conversation = calloc(1, sizeof *conversation);
    if (!conversation)
    {
        instance->error = 0x4007;
        goto connect_done;
    }
    conversation->client = id;
    conversation->server = server_handle;
    conversation->service = service;
    conversation->topic = topic;
    handle =
        lx_handle_create(LX_HANDLE_OPEN32_DDE_CONVERSATION, conversation, destroy_conversation);
    if (!handle)
    {
        free(conversation);
        instance->error = 0x4007;
        goto connect_done;
    }
    conversation->handle = handle;
    lx_host_monitor_lock(lock);
    conversation->next = dde_conversations;
    dde_conversations = conversation;
    lx_host_monitor_unlock(lock);
    ULONG ignored;
    dde_invoke(server_handle, 10000, 0x8072, 0, handle, topic, service, 0, 0, 0, &ignored);
connect_done:
    lx_handle_release(&client);
    return handle;
}

BOOL DdeDisconnect(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
        return FALSE;
    DdeConversation *conversation = borrow.resource;
    ULONG ignored;
    conversation_invoke(conversation,
                        conversation->server ? conversation->server : conversation->client,
                        REMOTE_TIMEOUT, 0x80C2, 0, 0, 0, 0, 0, 0, &ignored);
    BOOL result = lx_handle_close(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION);
    lx_handle_release(&borrow);
    return result;
}

static void note_transaction(DdeConversation *conversation, ULONG type, ULONG result)
{
    if ((type & ~0xCu) == 0x1030 && result)
        conversation->advise = TRUE;
    if (type == 0x8040)
        conversation->advise = FALSE;
}

/* Poke and execute answer with DDE_FACK or DDE_FBUSY; the others answer with a value. */
static ULONG transaction_status(ULONG type, ULONG result)
{
    if (type == 0x4090 || type == 0x4050)
        return result & 0x4000 ? 0x4000 : result & 0x8000 ? 0x8000 : 0;
    return result ? 0x8000 : 0;
}

static void complete_transaction(DdeRequest *request)
{
    LxHandleBorrow borrow = {0};
    if (lx_handle_borrow(request->conversation, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
    {
        note_transaction(borrow.resource, request->type, request->result);
        lx_handle_release(&borrow);
    }
    if (request->release_data)
        DdeFreeDataHandle(request->release_data);
    DdeRequest *reply = calloc(1, sizeof *reply);
    LxHandleBorrow client = {0};
    if (!reply || !borrow_any(request->client, &client))
    {
        free(reply);
        return;
    }
    ULONG thread = ((DdeInstance *)client.resource)->thread;
    lx_handle_release(&client);
    ULONG status = transaction_status(request->type, request->result);
    *reply = (DdeRequest){.thread = thread,
                          .instance = request->client,
                          .type = 0x8080, /* XTYP_XACT_COMPLETE */
                          .format = request->format,
                          .conversation = request->conversation,
                          .first = request->first,
                          .second = request->second,
                          .data = request->type == 0x20B0 ? request->result : status != 0,
                          .one = request->transaction,
                          .two = status,
                          .transaction = request->transaction,
                          .user = request->user};
    lx_host_monitor_lock(lock);
    queue_request(reply);
    lx_host_monitor_unlock(lock);
    open32PostThreadPacket(thread, OPEN32_DDE_PACKET);
}

static BOOL queue_transaction(DdeConversation *conversation, ULONG type, ULONG format, ULONG item,
                              ULONG data, ULONG temporary, PVOID status)
{
    /* A remote partner is reached from the client's own thread, on its next delivery. */
    BOOL forward = conversation->remote_process != 0;
    LxHandleBorrow server = {0};
    DdeRequest *request = calloc(1, sizeof *request);
    if (!request || !borrow_any(forward ? conversation->client : conversation->server, &server))
    {
        free(request);
        return FALSE;
    }
    ULONG thread = ((DdeInstance *)server.resource)->thread;
    lx_handle_release(&server);
    lx_host_monitor_lock(lock);
    ULONG transaction = ++next_transaction;
    *request = (DdeRequest){.thread = thread,
                            .instance = conversation->server,
                            .type = type,
                            .format = format,
                            .conversation = conversation->handle,
                            .first = conversation->topic,
                            .second = item,
                            .data = data,
                            .transaction = transaction,
                            .client = conversation->client,
                            .release_data = temporary,
                            .asynchronous = TRUE,
                            .forward = forward};
    queue_request(request);
    lx_host_monitor_unlock(lock);
    if (!open32PostThreadPacket(thread, OPEN32_DDE_PACKET))
    {
        lx_host_monitor_lock(lock);
        forget_request(request);
        lx_host_monitor_unlock(lock);
        free(request);
        return FALSE;
    }
    if (status)
        lx_guest_store_u32(status, transaction);
    return TRUE;
}

ULONG DdeClientTransaction(ULONG address, ULONG size, ULONG handle, ULONG item, ULONG format,
                           ULONG type, ULONG timeout, PVOID status)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
        return 0;
    DdeConversation *conversation = borrow.resource;
    LxHandleBorrow client = {0};
    ULONG result = 0, data = 0;
    BOOL temporary = FALSE;
    if (!borrow_instance(conversation->client, &client))
        goto transaction_done;
    if (type != 0x20B0 && type != 0x4090 && type != 0x4050 && (type & ~0xCu) != 0x1030 &&
        type != 0x8040)
    {
        ((DdeInstance *)client.resource)->error = 0x4006;
        goto transaction_done;
    }
    if (size == UINT32_MAX)
        data = address;
    else if (size)
    {
        LxGuestBorrow input = {0};
        if (!lx_execution_borrow(GLoaderState.execution, address, size, LX_GUEST_READ, &input))
        {
            ((DdeInstance *)client.resource)->error = 0x4006;
            goto transaction_done;
        }
        data = DdeCreateDataHandle(conversation->client, input.host, size, 0, item, format, 0);
        lx_execution_release(&input);
        temporary = TRUE;
        if (!data)
            goto transaction_done;
    }
    conversation->item = item;
    conversation->format = format;
    conversation->type = type;
    if (timeout == UINT32_MAX) /* TIMEOUT_ASYNC */
    {
        result =
            queue_transaction(conversation, type, format, item, data, temporary ? data : 0, status);
        if (result)
            temporary = FALSE;
        else
            ((DdeInstance *)client.resource)->error = 0x400C;
        goto transaction_done;
    }
    ULONG answer = 0;
    if (!conversation_invoke(conversation, conversation->server, timeout, type, format,
                             conversation->topic, item, data, 0, 0, &answer))
    {
        ((DdeInstance *)client.resource)->error = type == 0x20B0   ? 0x4002
                                                  : type == 0x4090 ? 0x400B
                                                  : type == 0x4050 ? 0x4005
                                                                   : 0x4000;
        goto transaction_done;
    }
    note_transaction(conversation, type, answer);
    if (type == 0x8040)
        answer = 1;
    ULONG acknowledged = transaction_status(type, answer);
    if (status)
        lx_guest_store_u32(status, acknowledged);
    if (!acknowledged)
        ((DdeInstance *)client.resource)->error = 0x4009;
    result = type == 0x20B0 ? answer : (acknowledged & 0x8000) != 0;
transaction_done:
    if (temporary && data)
        DdeFreeDataHandle(data);
    lx_handle_release(&client);
    lx_handle_release(&borrow);
    return result;
}

/* The asynchronous transaction with this id, pending or now reporting its completion. */
static DdeRequest *find_transaction(ULONG handle, ULONG transaction)
{
    if (dde_active && dde_active->transaction == transaction && dde_active->conversation == handle)
        return dde_active;
    for (DdeRequest *request = dde_requests; request; request = request->next)
        if (request->transaction == transaction && request->conversation == handle)
            return request;
    return NULL;
}

BOOL DdeSetUserHandle(ULONG handle, ULONG transaction, ULONG user)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
        return FALSE;
    DdeConversation *conversation = borrow.resource;
    BOOL valid = transaction == UINT32_MAX;
    if (valid)
        conversation->user = user;
    else
    {
        lx_host_monitor_lock(lock);
        DdeRequest *request = find_transaction(handle, transaction);
        if (request)
            request->user = user;
        valid = request != NULL;
        lx_host_monitor_unlock(lock);
        if (!valid)
            open32DdeSetError(conversation->client, 0x4006);
    }
    lx_handle_release(&borrow);
    return valid;
}

ULONG DdeQueryConvInfo(ULONG handle, ULONG transaction, PVOID output)
{
    LxHandleBorrow borrow = {0};
    if (!output || lx_guest_load_u32(output) != 100 ||
        !lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
        return 0;
    DdeConversation *conversation = borrow.resource;
    ULONG user = conversation->user;
    if (transaction != UINT32_MAX && transaction)
    {
        lx_host_monitor_lock(lock);
        DdeRequest *request = find_transaction(handle, transaction);
        BOOL pending = request != NULL;
        if (request)
            user = request->user;
        lx_host_monitor_unlock(lock);
        if (!pending)
        {
            open32DdeSetError(conversation->client, 0x4006);
            lx_handle_release(&borrow);
            return 0;
        }
    }
    memset(output, 0, 100);
    ULONG fields[] = {100,
                      user,
                      handle,
                      conversation->service,
                      conversation->service,
                      conversation->topic,
                      conversation->item,
                      conversation->format,
                      conversation->type,
                      0x10 | 0x1,
                      2,
                      0,
                      0,
                      40,
                      0,
                      0,
                      1004,
                      0x409,
                      0,
                      0,
                      0,
                      0,
                      0,
                      0,
                      0};
    for (unsigned i = 0; i < 25; i++)
        lx_guest_store_u32((uint8 *)output + i * 4, fields[i]);
    lx_handle_release(&borrow);
    return 100;
}

BOOL DdeAbandonTransaction(ULONG id, ULONG handle, ULONG transaction)
{
    LxHandleBorrow instance = {0};
    if (!borrow_instance(id, &instance))
        return FALSE;
    BOOL valid = !transaction;
    if (handle)
    {
        LxHandleBorrow conversation = {0};
        BOOL owned = lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION, &conversation) &&
                     ((DdeConversation *)conversation.resource)->client == id;
        lx_handle_release(&conversation);
        if (!owned)
            transaction = 0, valid = FALSE;
    }
    /* A zero transaction abandons everything on the conversation, or on the instance. */
    lx_host_monitor_lock(lock);
    for (DdeRequest *request = dde_requests; request; request = request->next)
        if (request->asynchronous && request->client == id &&
            (!handle || request->conversation == handle) &&
            (!transaction || request->transaction == transaction))
        {
            request->abandoned = TRUE;
            valid = TRUE;
        }
    lx_host_monitor_unlock(lock);
    if (!valid)
        ((DdeInstance *)instance.resource)->error = 0x4006;
    lx_handle_release(&instance);
    return valid;
}

BOOL DdePostAdvise(ULONG id, ULONG topic, ULONG item)
{
    LxHandleBorrow server = {0};
    if (!borrow_instance(id, &server))
        return FALSE;
    ULONG *handles = NULL;
    size_t count = 0;
    lx_host_monitor_lock(lock);
    for (DdeConversation *conversation = dde_conversations; conversation;
         conversation = conversation->next)
        if (conversation->server == id && conversation->advise &&
            (!topic || topic == conversation->topic) && (!item || item == conversation->item))
        {
            ULONG *next = realloc(handles, (count + 1) * sizeof *next);
            if (!next)
            {
                free(handles);
                lx_host_monitor_unlock(lock);
                lx_handle_release(&server);
                return FALSE;
            }
            handles = next;
            handles[count++] = conversation->handle;
        }
    lx_host_monitor_unlock(lock);
    for (size_t i = 0; i < count; i++)
    {
        LxHandleBorrow borrow = {0};
        if (!lx_handle_borrow(handles[i], LX_HANDLE_OPEN32_DDE_CONVERSATION, &borrow))
            continue;
        DdeConversation *conversation = borrow.resource;
        ULONG ignored;
        ULONG data =
            dde_callback(server.resource, 0x2022, conversation->format, conversation->handle,
                         conversation->topic, conversation->item, 0, 0, 0);
        conversation_invoke(conversation, conversation->client, REMOTE_TIMEOUT, 0x4010,
                            conversation->format, conversation->topic, conversation->item, data, 0,
                            0, &ignored);
        if (data)
            DdeFreeDataHandle(data);
        lx_handle_release(&borrow);
    }
    free(handles);
    lx_handle_release(&server);
    return TRUE;
}

static void cleanup_dde_owned(ULONG id)
{
    lx_host_monitor_lock(lock);
    for (DdeRequest *request = dde_requests; request; request = request->next)
        if (request->instance == id || request->client == id)
            request->abandoned = TRUE;
    lx_host_monitor_wake_all(lock);
    lx_host_monitor_unlock(lock);
    for (;;)
    {
        ULONG handle = 0;
        lx_host_monitor_lock(lock);
        for (DdeConversation **link = &dde_conversations; *link; link = &(*link)->next)
            if ((*link)->client == id || (*link)->server == id)
            {
                DdeConversation *conversation = *link;
                handle = conversation->handle;
                *link = conversation->next;
                break;
            }
        lx_host_monitor_unlock(lock);
        if (!handle)
            break;
        lx_handle_close(handle, LX_HANDLE_OPEN32_DDE_CONVERSATION);
    }
    for (;;)
    {
        ULONG handle = 0;
        lx_host_monitor_lock(lock);
        for (DdeData **link = &dde_data; *link; link = &(*link)->next)
            if ((*link)->owner == id)
            {
                DdeData *data = *link;
                handle = data->handle;
                *link = data->next;
                break;
            }
        lx_host_monitor_unlock(lock);
        if (!handle)
            break;
        lx_handle_close(handle, LX_HANDLE_OPEN32_DDE_DATA);
    }
}
