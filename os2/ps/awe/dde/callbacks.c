/* Public domain. No rights reserved. Wholly AI-authored. */
/* DDE callbacks always run on the instance's owning thread: requests from other threads are queued
   for it, through an x86 argument thunk. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "emulator/handles.h"
#include "host/clock.h"
#include "os2/ps/awe/dde/dde.h"

DdeConversation *dde_conversations;

static LxGuestMapping *dde_callback_code;

ULONG dde_callback(DdeInstance *instance, ULONG type, ULONG format, ULONG conversation, ULONG first,
                   ULONG second, ULONG data, ULONG one, ULONG two)
{
    if (!instance || instance->owner != lxHostThreadState() || !instance->callback)
        return 0;
    if (!dde_callback_code)
    {
        static const uint8 code[] = {0x8B, 0x44, 0x24, 0x04, 0xFF, 0x70, 0x1C, 0xFF, 0x70,
                                     0x18, 0xFF, 0x70, 0x14, 0xFF, 0x70, 0x10, 0xFF, 0x70,
                                     0x0C, 0xFF, 0x70, 0x08, 0xFF, 0x70, 0x04, 0xFF, 0x30,
                                     0xFF, 0x50, 0x20, 0x83, 0xC4, 0x20, 0xC3};
        dde_callback_code = lx_execution_map(GProcessState.execution, 0, 4096, 3, 0);
        if (!dde_callback_code)
            return 0;
        memcpy(lx_execution_mapping_host(dde_callback_code), code, sizeof code);
        if (!lx_execution_protect(GProcessState.execution, dde_callback_code,
                                  LX_GUEST_READ | LX_GUEST_EXECUTE))
        {
            lx_execution_unmap(GProcessState.execution, dde_callback_code);
            dde_callback_code = NULL;
            return 0;
        }
    }
    LxGuestMapping *arguments = lx_execution_map(GProcessState.execution, 0, 4096, 3, 0);
    if (!arguments)
        return 0;
    ULONG values[] = {type, format, conversation,      first, second, data,
                      one,  two,    instance->callback};
    for (unsigned i = 0; i < 9; i++)
        lx_guest_store_u32((uint8 *)lx_execution_mapping_host(arguments) + i * 4, values[i]);
    ULONG result = lx_execution_callback32_4(GProcessState.execution,
                                             lx_execution_mapping_address(dde_callback_code),
                                             lx_execution_mapping_address(arguments), 0, 0, 0);
    lx_execution_unmap(GProcessState.execution, arguments);
    return result;
}

DdeRequest *dde_requests;

_Thread_local DdeRequest *dde_active; /* The request whose callback is running here. */

ULONG dde_next_transaction;

BOOL dde_borrow_any(ULONG handle, LxHandleBorrow *borrow)
{
    return lx_handle_borrow(handle, LX_HANDLE_OPEN32_DDE, borrow);
}

void dde_queue_request(DdeRequest *request)
{
    DdeRequest **link = &dde_requests;
    while (*link)
        link = &(*link)->next;
    request->next = NULL;
    *link = request;
}

void dde_forget_request(DdeRequest *request)
{
    for (DdeRequest **link = &dde_requests; *link; link = &(*link)->next)
        if (*link == request)
        {
            *link = request->next;
            break;
        }
}

ULONG dde_run_callback(const DdeRequest *request)
{
    LxHandleBorrow target = {0};
    if (!dde_borrow_instance(request->instance, &target))
        return 0;
    ULONG result =
        dde_callback(target.resource, request->type, request->format, request->conversation,
                     request->first, request->second, request->data, request->one, request->two);
    lx_handle_release(&target);
    return result;
}

void open32DdeDeliver(void)
{
    if (!dde_lock)
        return;
    ULONG thread = GetCurrentThreadId();
    for (;;)
    {
        lx_host_monitor_lock(dde_lock);
        DdeRequest *request = dde_requests;
        while (request && (request->thread != thread || request->done))
            request = request->next;
        if (request)
            dde_forget_request(request);
        lx_host_monitor_unlock(dde_lock);
        if (!request)
            return;
        if (request->remote_process && !request->abandoned)
        {
            dde_serve_remote(request);
            free(request->payload);
            free(request);
            continue;
        }
        if (request->abandoned)
        {
            lx_host_monitor_lock(dde_lock);
            BOOL waited = request->waiting;
            request->done = TRUE;
            lx_host_monitor_wake_all(dde_lock);
            lx_host_monitor_unlock(dde_lock);
            free(request->payload);
            request->payload = NULL;
            if (!waited)
                free(request);
            continue;
        }
        DdeRequest *outer = dde_active;
        dde_active = request;
        ULONG result =
            request->forward ? dde_forward_transaction(request) : dde_run_callback(request);
        dde_active = outer;
        lx_host_monitor_lock(dde_lock);
        request->result = result;
        request->done = TRUE;
        BOOL waiting = request->waiting;
        lx_host_monitor_wake_all(dde_lock);
        lx_host_monitor_unlock(dde_lock);
        if (request->asynchronous)
            dde_complete_transaction(request);
        if (!waiting)
            free(request);
    }
}

/* Runs a callback on the instance's thread, waiting up to timeout milliseconds. Requests
   addressed to this thread are served meanwhile, so two threads cannot block each other. */
BOOL dde_invoke(ULONG instance, ULONG timeout, ULONG type, ULONG format, ULONG conversation,
                ULONG first, ULONG second, ULONG data, ULONG one, ULONG two, ULONG *result)
{
    LxHandleBorrow target = {0};
    if (!dde_borrow_any(instance, &target))
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
    lx_host_monitor_lock(dde_lock);
    dde_queue_request(request);
    lx_host_monitor_unlock(dde_lock);
    if (!open32PostThreadPacket(thread, OPEN32_DDE_PACKET))
    {
        lx_host_monitor_lock(dde_lock);
        dde_forget_request(request);
        lx_host_monitor_unlock(dde_lock);
        free(request);
        return FALSE;
    }
    uint64 deadline = lx_host_monotonic_us() + (uint64)timeout * 1000;
    BOOL done;
    for (;;)
    {
        open32DdeDeliver();
        lx_host_monitor_lock(dde_lock);
        done = request->done;
        if (!done && lx_host_monotonic_us() < deadline)
            lx_host_monitor_wait(dde_lock, 20);
        done = request->done;
        BOOL expired = !done && lx_host_monotonic_us() >= deadline;
        if (expired)
        {
            /* The owner frees a request it finds abandoned. */
            request->abandoned = TRUE;
            request->waiting = FALSE;
        }
        lx_host_monitor_unlock(dde_lock);
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

void dde_notify_peers(DdeInstance *instance, ULONG type, ULONG skip, ULONG atom)
{
    dde_notify_processes(type, skip, atom);
    ULONG *threads = NULL;
    size_t count = 0;
    lx_host_monitor_lock(dde_lock);
    for (DdeInstance *peer = dde_instances; peer; peer = peer->next)
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
            dde_queue_request(request);
        }
    lx_host_monitor_unlock(dde_lock);
    for (size_t i = 0; i < count; i++)
        open32PostThreadPacket(threads[i], OPEN32_DDE_PACKET);
    free(threads);
}
