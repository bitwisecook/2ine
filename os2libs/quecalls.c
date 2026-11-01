/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native.h"
#include "quecalls.h"

#include "guest/handles.h"
#include "guest/abi.h"
#include "os2libs/doscalls.h"
#include "hal/host.h"
#include "hal/host_mailbox.h"
#include "hal/host_thread.h"
#include "hal/host_sync.h"

typedef struct Queue
{
    struct Queue *next;
    char *name;
    HQUEUE handle;
    BOOL private_writer;
    LxHostMailbox *mailbox;
    ULONG owner, flags;
    LxHostMutex *mutex;
    LxHostThread *notifier;
    HEV event;
    uint32 stopped, revision;
} Queue;
typedef struct Packet
{
    ULONG sender, request, length, address;
} Packet;
enum
{
    QUEUE_PAYLOAD_LIMIT = 65536
};
#include "os2libs/generated/quecalls-lx.h"

static LxHostOnce catalog_once = LX_HOST_ONCE_INIT;
static LxHostMutex *catalog_lock;
static Queue *catalog;
static void initialize_catalog(void)
{
    catalog_lock = lx_host_mutex_create(0, 0);
    if (!catalog_lock)
        abort();
}
static void lock_catalog(void)
{
    lx_host_once(&catalog_once, initialize_catalog);
    lx_host_mutex_lock(catalog_lock, UINT32_MAX);
}
static void destroy_queue(void *value)
{
    Queue *queue = value;
    __atomic_store_n(&queue->stopped, 1, __ATOMIC_RELEASE);
    if (queue->notifier)
        lx_host_thread_join(queue->notifier, NULL);
    lock_catalog();
    for (Queue **link = &catalog; *link; link = &(*link)->next)
        if (*link == queue)
        {
            *link = queue->next;
            break;
        }
    lx_host_mutex_unlock(catalog_lock);
    lx_host_mailbox_close(queue->mailbox);
    free(queue->name);
    if (queue->mutex)
        lx_host_mutex_destroy(queue->mutex);
    free(queue);
}
static int notify_revision(Queue *queue)
{
    lx_host_mutex_lock(queue->mutex, UINT32_MAX);
    uint32 revision;
    int failed = lx_host_mailbox_revision(queue->mailbox, &revision);
    if (!failed && queue->event && revision != queue->revision)
    {
        APIRET result = DosPostEventSem(queue->event);
        failed = result && result != ERROR_ALREADY_POSTED && result != ERROR_TOO_MANY_POSTS;
        if (!failed)
            queue->revision = revision;
    }
    lx_host_mutex_unlock(queue->mutex);
    return failed;
}
static void *notify_queue(void *value)
{
    Queue *queue = value;
    while (!__atomic_load_n(&queue->stopped, __ATOMIC_ACQUIRE))
    {
        if (notify_revision(queue))
            break;
        lx_host_sleep_ms(1);
    }
    return NULL;
}
static APIRET register_notification(Queue *queue, HEV event)
{
    if (!event)
        return ERROR_INVALID_PARAMETER;
    lx_host_mutex_lock(queue->mutex, UINT32_MAX);
    APIRET result = NO_ERROR;
    if (queue->event && queue->event != event)
        result = ERROR_INVALID_PARAMETER;
    else if (!queue->event && event)
    {
        ULONG posts;
        if (DosQueryEventSem(event, &posts))
            result = ERROR_INVALID_PARAMETER;
        else if (lx_host_mailbox_revision(queue->mailbox, &queue->revision))
            result = ERROR_QUE_MEMORY_ERROR;
        else
        {
            queue->event = event;
            queue->notifier = lx_host_thread_create(notify_queue, queue, NULL);
            if (!queue->notifier)
            {
                queue->event = 0;
                result = ERROR_QUE_NO_MEMORY;
            }
        }
    }
    lx_host_mutex_unlock(queue->mutex);
    return result;
}
static APIRET open_queue(PHQUEUE output, PPID owner, const char *name, int create, ULONG flags,
                         int reuse)
{
    if (!output)
        return ERROR_INVALID_PARAMETER;
    if (!name || strncasecmp(name, "\\QUEUES\\", 8) || !name[8] || strlen(name) > 255)
        return ERROR_QUE_INVALID_NAME;
    if ((flags & ~7u) || (flags & 3) == 3)
        return ERROR_INVALID_PARAMETER;
    if (!create && reuse)
    {
        lock_catalog();
        for (Queue *q = catalog; q; q = q->next)
            if (!q->private_writer && !strcasecmp(q->name, name) &&
                !__atomic_load_n(&q->stopped, __ATOMIC_ACQUIRE))
            {
                *output = q->handle;
                if (owner)
                    *owner = q->owner;
                lx_host_mutex_unlock(catalog_lock);
                return NO_ERROR;
            }
        lx_host_mutex_unlock(catalog_lock);
    }
    uint64_t hash = 1469598103934665603ULL;
    const char *ball = getenv("TWOINE_BALL");
    for (const char *p = ball ? ball : ""; *p; p++)
        hash = (hash ^ (unsigned char)*p) * 1099511628211ULL;
    hash = (hash ^ 0xff) * 1099511628211ULL;
    for (const char *p = name; *p; p++)
    {
        unsigned char c = *p;
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        hash = (hash ^ c) * 1099511628211ULL;
    }
    char identifier[40];
    snprintf(identifier, sizeof identifier, "queue-%016llx", (unsigned long long)hash);
    Queue *queue = calloc(1, sizeof *queue);
    if (!queue)
        return ERROR_QUE_NO_MEMORY;
    int result = lx_host_mailbox_open(getenv("TWOINE_IPC_DIR"), identifier, create, flags & 3,
                                      &queue->mailbox, &queue->owner);
    if (result)
    {
        free(queue);
        return result == 1   ? ERROR_QUE_NAME_NOT_EXIST
               : result == 2 ? ERROR_QUE_DUPLICATE
                             : ERROR_QUE_NO_MEMORY;
    }
    queue->flags = flags;
    queue->private_writer = !create && !reuse;
    queue->mutex = lx_host_mutex_create(0, 0);
    if (!queue->mutex)
    {
        destroy_queue(queue);
        return ERROR_QUE_NO_MEMORY;
    }
    HQUEUE handle = lx_handle_create(LX_HANDLE_QUEUE, queue, destroy_queue);
    if (!handle)
    {
        destroy_queue(queue);
        return ERROR_QUE_NO_MEMORY;
    }
    queue->name = strdup(name);
    if (!queue->name)
    {
        lx_handle_close(handle, LX_HANDLE_QUEUE);
        return ERROR_QUE_NO_MEMORY;
    }
    queue->handle = handle;
    lock_catalog();
    queue->next = catalog;
    catalog = queue;
    lx_host_mutex_unlock(catalog_lock);
    *output = handle;
    if (owner)
        *owner = queue->owner;
    return NO_ERROR;
}
APIRET DosCreateQueue(PHQUEUE phq, ULONG priority, PSZ name)
{
    TRACE_NATIVE("DosCreateQueue(%p, %u, '%s')", phq, (uint)priority, name);
    return open_queue(phq, NULL, name, 1, priority, 0);
}
APIRET DosCloseQueue(HQUEUE handle)
{
    TRACE_NATIVE("DosCloseQueue(%u)", (uint)handle);
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_QUEUE, &borrow))
        return ERROR_QUE_INVALID_HANDLE;
    Queue *queue = borrow.resource;
    __atomic_store_n(&queue->stopped, 1, __ATOMIC_RELEASE);
    APIRET result = lx_handle_close(handle, LX_HANDLE_QUEUE) ? NO_ERROR : ERROR_QUE_INVALID_HANDLE;
    lx_handle_release(&borrow);
    return result;
}
APIRET DosOpenQueue(PPID owner, PHQUEUE output, PSZ name)
{
    TRACE_NATIVE("DosOpenQueue(%p, %p, '%s')", owner, output, name);
    if (!owner)
        return ERROR_INVALID_PARAMETER;
    return open_queue(output, owner, name, 0, 0, 1);
}
APIRET lx_queue_open_writer(PPID owner, PHQUEUE output, PSZ name)
{
    return open_queue(output, owner, name, 0, 0, 0);
}
typedef struct ReceiveCall
{
    HQUEUE handle;
    PREQUESTDATA request;
    PULONG length;
    PPVOID output;
    PULONG element;
    BOOL32 nowait;
    PBYTE priority;
    HEV event;
    int peek;
    LxHandleBorrow borrow;
    uint8 *bytes;
} ReceiveCall;
static void receive_cleanup(void *argument)
{
    ReceiveCall *call = argument;
    free(call->bytes);
    lx_handle_release(&call->borrow);
}
static uint32 receive_operation(void *argument)
{
    ReceiveCall *call = argument;
    HQUEUE handle = call->handle;
    PREQUESTDATA request = call->request;
    PULONG length = call->length, element = call->element;
    PPVOID output = call->output;
    BOOL32 nowait = call->nowait;
    PBYTE priority = call->priority;
    HEV event = call->event;
    int peek = call->peek;
    if (!request || !length || !output || !priority || !element)
        return ERROR_INVALID_PARAMETER;
    if (nowait > 1)
        return 433; /* ERROR_QUE_INVALID_WAIT */
    LxHandleBorrow *borrow = &call->borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_QUEUE, borrow))
        return ERROR_QUE_INVALID_HANDLE;
    Queue *queue = borrow->resource;
    if (queue->owner != GLoaderState.pib.pib_ulpid)
    {
        return ERROR_QUE_PROC_NOT_OWNED;
    }
    if (nowait)
    {
        APIRET result = register_notification(queue, event);
        if (result)
        {
            return result;
        }
    }
    uint8 *bytes = malloc(sizeof(Packet) + QUEUE_PAYLOAD_LIMIT);
    if (!bytes)
    {
        return ERROR_QUE_NO_MEMORY;
    }
    call->bytes = bytes;
    size_t size = 0;
    unsigned importance;
    uint32 token = *element;
    int received;
    while ((received =
                lx_host_mailbox_receive(queue->mailbox, bytes, sizeof(Packet) + QUEUE_PAYLOAD_LIMIT,
                                        &size, &importance, &token, peek)) == 1 &&
           !nowait && !__atomic_load_n(&queue->stopped, __ATOMIC_ACQUIRE))
        lx_host_sleep_ms(10);
    APIRET result = received == 1   ? ERROR_QUE_EMPTY
                    : received == 2 ? ERROR_QUE_ELEMENT_NOT_EXIST
                    : received == 3 ? ERROR_QUE_PREV_AT_END
                    : received      ? ERROR_QUE_MEMORY_ERROR
                                    : NO_ERROR;
    if (__atomic_load_n(&queue->stopped, __ATOMIC_ACQUIRE))
        result = ERROR_QUE_INVALID_HANDLE;
    if (!result)
    {
        Packet packet;
        if (size < sizeof packet)
            result = ERROR_QUE_MEMORY_ERROR;
        else
        {
            memcpy(&packet, bytes, sizeof packet);
            size_t payload = size - sizeof packet;
            if (payload && packet.length != payload)
                result = ERROR_QUE_MEMORY_ERROR;
            else
            {
                void *memory = (void *)(uintptr_t)packet.address;
                if (packet.length && payload &&
                    (!packet.address || packet.sender != GLoaderState.pib.pib_ulpid))
                {
                    result = DosAllocMem(&memory, packet.length, PAG_READ | PAG_WRITE | PAG_COMMIT);
                    if (!result)
                        memcpy(memory, bytes + sizeof packet, packet.length);
                }
                if (!result)
                {
                    request->pid = packet.sender;
                    request->ulData = packet.request;
                    *length = packet.length;
                    *output = memory;
                    *priority = (queue->flags & 3) == 2 ? importance : 0;
                    *element = token;
                }
            }
        }
    }
    return result;
}
static APIRET receive_queue(HQUEUE handle, PREQUESTDATA request, PULONG length, PPVOID output,
                            PULONG element, BOOL32 nowait, PBYTE priority, HEV event, int peek)
{
    ReceiveCall call = {0};
    call.handle = handle;
    call.request = request;
    call.length = length;
    call.output = output;
    call.element = element;
    call.nowait = nowait;
    call.priority = priority;
    call.event = event;
    call.peek = peek;
    return lx_host_guarded_call(receive_operation, &call, receive_cleanup, &call);
}
APIRET DosPeekQueue(HQUEUE handle, PREQUESTDATA request, PULONG length, PPVOID output,
                    PULONG element, BOOL32 nowait, PBYTE priority, HEV event)
{
    TRACE_NATIVE("DosPeekQueue(%u)", (uint)handle);
    return receive_queue(handle, request, length, output, element, nowait, priority, event, 1);
}
APIRET DosPurgeQueue(HQUEUE handle)
{
    TRACE_NATIVE("DosPurgeQueue(%u)", (uint)handle);
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_QUEUE, &borrow))
        return ERROR_QUE_INVALID_HANDLE;
    Queue *queue = borrow.resource;
    uint32 count;
    APIRET result = queue->owner != GLoaderState.pib.pib_ulpid         ? ERROR_QUE_PROC_NOT_OWNED
                    : lx_host_mailbox_count(queue->mailbox, &count, 1) ? ERROR_QUE_MEMORY_ERROR
                                                                       : NO_ERROR;
    lx_handle_release(&borrow);
    return result;
}
APIRET DosQueryQueue(HQUEUE handle, PULONG count)
{
    TRACE_NATIVE("DosQueryQueue(%u, %p)", (uint)handle, count);
    if (!count)
        return ERROR_INVALID_PARAMETER;
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_QUEUE, &borrow))
        return ERROR_QUE_INVALID_HANDLE;
    Queue *queue = borrow.resource;
    APIRET result =
        lx_host_mailbox_count(queue->mailbox, count, 0) ? ERROR_QUE_MEMORY_ERROR : NO_ERROR;
    lx_handle_release(&borrow);
    return result;
}
APIRET DosReadQueue(HQUEUE handle, PREQUESTDATA request, PULONG length, PPVOID output,
                    ULONG element, BOOL32 nowait, PBYTE priority, HEV event)
{
    TRACE_NATIVE("DosReadQueue(%u, %p, %p, %p, %u, %u, %p, %u)", (uint)handle, request, length,
                 output, (uint)element, (uint)nowait, priority, (uint)event);
    return receive_queue(handle, request, length, output, &element, nowait, priority, event, 0);
}
APIRET DosWriteQueue(HQUEUE handle, ULONG request, ULONG length, PVOID data, ULONG priority)
{
    TRACE_NATIVE("DosWriteQueue(%u, %u, %u, %p, %u)", (uint)handle, (uint)request, (uint)length,
                 data, (uint)priority);
    if (priority > 15)
        priority = 15;
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_QUEUE, &borrow))
        return ERROR_QUE_INVALID_HANDLE;
    Queue *queue = borrow.resource;
    uint32 address = (uint32)(uintptr_t)data;
    size_t copied = 0;
    if (length && length <= QUEUE_PAYLOAD_LIMIT && data)
    {
        if (queue->private_writer)
        {
            copied = length;
            address = 0;
        }
        else if (!GLoaderState.execution || lx_execution_address(GLoaderState.execution, data,
                                                                 length, LX_GUEST_READ, &address))
            copied = length;
    }
    uint8 *bytes = malloc(sizeof(Packet) + copied);
    if (!bytes)
    {
        lx_handle_release(&borrow);
        return ERROR_QUE_NO_MEMORY;
    }
    Packet packet = {GLoaderState.pib.pib_ulpid, request, length, address};
    memcpy(bytes, &packet, sizeof packet);
    if (copied)
        memcpy(bytes + sizeof packet, data, copied);
    APIRET result = lx_host_mailbox_send(queue->mailbox, bytes, sizeof packet + copied, priority)
                        ? ERROR_QUE_UNABLE_TO_ADD
                        : NO_ERROR;
    free(bytes);
    if (!result)
        notify_revision(queue);
    lx_handle_release(&borrow);
    return result;
}
