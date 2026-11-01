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

static void destroy_queue(void *value)
{
    Queue *queue = value;
    __atomic_store_n(&queue->stopped, 1, __ATOMIC_RELEASE);
    if (queue->notifier)
        lx_host_thread_join(queue->notifier, NULL);
    lx_host_mailbox_close(queue->mailbox);
    if (queue->mutex)
        lx_host_mutex_destroy(queue->mutex);
    free(queue);
}
static void *notify_queue(void *value)
{
    Queue *queue = value;
    uint32 previous = queue->revision;
    while (!__atomic_load_n(&queue->stopped, __ATOMIC_ACQUIRE))
    {
        uint32 revision;
        if (lx_host_mailbox_revision(queue->mailbox, &revision))
            break;
        if (revision != previous)
        {
            APIRET result = DosPostEventSem(queue->event);
            if (result && result != ERROR_ALREADY_POSTED && result != ERROR_TOO_MANY_POSTS)
                break;
            previous = revision;
        }
        lx_host_sleep_ms(1);
    }
    return NULL;
}
static APIRET register_notification(Queue *queue, HEV event)
{
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
static APIRET open_queue(PHQUEUE output, PPID owner, const char *name, int create, ULONG flags)
{
    if (!output || !name || strncasecmp(name, "\\QUEUES\\", 8) || strlen(name) > 255)
        return ERROR_QUE_INVALID_NAME;
    if ((flags & ~7u) || (flags & 3) == 3)
        return ERROR_QUE_INVALID_PRIORITY;
    uint64_t hash = 1469598103934665603ULL;
    const char *bottle = getenv("TWOINE_BOTTLE");
    for (const char *p = bottle ? bottle : ""; *p; p++)
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
    *output = handle;
    if (owner)
        *owner = queue->owner;
    return NO_ERROR;
}
APIRET DosCreateQueue(PHQUEUE phq, ULONG priority, PSZ name)
{
    TRACE_NATIVE("DosCreateQueue(%p, %u, '%s')", phq, (uint)priority, name);
    return open_queue(phq, NULL, name, 1, priority);
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
    return open_queue(output, owner, name, 0, 0);
}
static APIRET receive_queue(HQUEUE handle, PREQUESTDATA request, PULONG length, PPVOID output,
                            PULONG element, BOOL32 nowait, PBYTE priority, HEV event, int peek)
{
    if (!request || !length || !output || !priority || !element)
        return ERROR_INVALID_PARAMETER;
    if (nowait > 1)
        return 433; /* ERROR_QUE_INVALID_WAIT */
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_QUEUE, &borrow))
        return ERROR_QUE_INVALID_HANDLE;
    Queue *queue = borrow.resource;
    if (queue->owner != GLoaderState.pib.pib_ulpid)
    {
        lx_handle_release(&borrow);
        return ERROR_QUE_PROC_NOT_OWNED;
    }
    if (nowait)
    {
        APIRET result = register_notification(queue, event);
        if (result)
        {
            lx_handle_release(&borrow);
            return result;
        }
    }
    uint8 *bytes = malloc(sizeof(Packet) + QUEUE_PAYLOAD_LIMIT);
    if (!bytes)
    {
        lx_handle_release(&borrow);
        return ERROR_QUE_NO_MEMORY;
    }
    size_t size = 0;
    unsigned importance;
    uint32 token = *element;
    int received;
    while ((received =
                lx_host_mailbox_receive(queue->mailbox, bytes, sizeof(Packet) + QUEUE_PAYLOAD_LIMIT,
                                        &size, &importance, &token, peek)) == 1 &&
           !nowait && !__atomic_load_n(&queue->stopped, __ATOMIC_ACQUIRE))
        lx_host_sleep_ms(10);
    APIRET result = received == 1 ? ERROR_QUE_EMPTY : received ? ERROR_QUE_MEMORY_ERROR : NO_ERROR;
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
            if (packet.length != size - sizeof packet)
                result = ERROR_QUE_MEMORY_ERROR;
            else
            {
                void *memory = (void *)(uintptr_t)packet.address;
                if (packet.length &&
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
                    *priority = importance;
                    *element = token;
                }
            }
        }
    }
    free(bytes);
    lx_handle_release(&borrow);
    return result;
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
    if (priority > 15 || length > QUEUE_PAYLOAD_LIMIT || (length && !data))
        return ERROR_INVALID_PARAMETER;
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_QUEUE, &borrow))
        return ERROR_QUE_INVALID_HANDLE;
    uint8 *bytes = malloc(sizeof(Packet) + length);
    if (!bytes)
    {
        lx_handle_release(&borrow);
        return ERROR_QUE_NO_MEMORY;
    }
    uint32 address = (uint32)(uintptr_t)data;
    if (GLoaderState.execution && length &&
        !lx_execution_address(GLoaderState.execution, data, length, LX_GUEST_READ, &address))
        address = 0; /* Host-generated session notifications carry copied bytes. */
    Packet packet = {GLoaderState.pib.pib_ulpid, request, length, address};
    memcpy(bytes, &packet, sizeof packet);
    if (length)
        memcpy(bytes + sizeof packet, data, length);
    Queue *queue = borrow.resource;
    APIRET result = lx_host_mailbox_send(queue->mailbox, bytes, sizeof packet + length, priority)
                        ? ERROR_QUE_UNABLE_TO_ADD
                        : NO_ERROR;
    free(bytes);
    lx_handle_release(&borrow);
    return result;
}
