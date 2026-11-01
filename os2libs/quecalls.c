/* Public domain. No rights reserved. Wholly AI-authored. */
/* Process-local OS/2 named queues retain sender addresses without copying data. */
#include "os2native.h"
#include "quecalls.h"
#include "guest/handles.h"
#include "hal/host_sync.h"
#include "hal/host_thread.h"
#include "hal/host_library.h"

typedef struct QueueEntry
{
    struct QueueEntry *next;
    ULONG id, request, size, priority;
    PID sender;
    PVOID data;
} QueueEntry;
typedef struct Queue
{
    struct Queue *next;
    char *name;
    HQUEUE handle;
    ULONG order, next_id, count;
    PID owner;
    HEV event;
    BOOL closed;
    QueueEntry *head;
} Queue;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *monitor;
static Queue *queues;
static void initialize(void)
{
    monitor = lx_host_monitor_create();
    if (!monitor)
        abort();
}
static void lock_queues(void)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
}
static BOOL valid_name(PSZ name)
{
    return name && !strncasecmp(name, "\\QUEUES\\", 8) && name[8] && strlen(name) <= 255;
}
static void purge(Queue *queue)
{
    while (queue->head)
    {
        QueueEntry *entry = queue->head;
        queue->head = entry->next;
        free(entry);
    }
    queue->count = 0;
}
static void destroy_queue(void *value)
{
    Queue *queue = value;
    purge(queue);
    free(queue->name);
    free(queue);
}
static void post_event(HEV event)
{
    if (!event)
        return;
    typedef APIRET(OS2API * PostFn)(HEV);
    for (LxModule *module = GLoaderState.loaded_modules; module; module = module->next)
        if (module->nativelib && !strcasecmp(module->name, "DOSCALLS"))
        {
            PostFn post = (PostFn)lx_host_library_symbol(module->nativelib, "DosPostEventSem");
            if (post)
                post(event);
            return;
        }
}
APIRET DosCreateQueue(PHQUEUE output, ULONG flags, PSZ name)
{
    TRACE_NATIVE("DosCreateQueue(%p, %u, '%s')", output, flags, name);
    if (!output || (flags & ~7u) || (flags & 3) == 3)
        return ERROR_INVALID_PARAMETER;
    if (!valid_name(name))
        return ERROR_QUE_INVALID_NAME;
    Queue *queue = calloc(1, sizeof *queue);
    if (!queue)
        return ERROR_QUE_NO_MEMORY;
    queue->name = strdup(name);
    if (!queue->name)
    {
        free(queue);
        return ERROR_QUE_NO_MEMORY;
    }
    queue->order = flags & 3;
    queue->owner = GLoaderState.pib.pib_ulpid;
    lock_queues();
    APIRET result = NO_ERROR;
    for (Queue *q = queues; q; q = q->next)
        if (!strcasecmp(q->name, name))
        {
            result = ERROR_QUE_DUPLICATE;
            break;
        }
    if (!result)
    {
        queue->handle = lx_handle_create(LX_HANDLE_QUEUE, queue, destroy_queue);
        if (!queue->handle)
            result = ERROR_QUE_NO_MEMORY;
        else
        {
            queue->next = queues;
            queues = queue;
            *output = queue->handle;
        }
    }
    lx_host_monitor_unlock(monitor);
    if (result)
        destroy_queue(queue);
    return result;
}
APIRET DosOpenQueue(PPID owner, PHQUEUE output, PSZ name)
{
    TRACE_NATIVE("DosOpenQueue(%p, %p, '%s')", owner, output, name);
    if (!owner || !output)
        return ERROR_INVALID_PARAMETER;
    if (!valid_name(name))
        return ERROR_QUE_INVALID_NAME;
    lock_queues();
    APIRET result = ERROR_QUE_NAME_NOT_EXIST;
    for (Queue *q = queues; q; q = q->next)
        if (!strcasecmp(q->name, name))
        {
            *owner = q->owner;
            *output = q->handle;
            result = NO_ERROR;
            break;
        }
    lx_host_monitor_unlock(monitor);
    return result;
}
APIRET DosCloseQueue(HQUEUE handle)
{
    TRACE_NATIVE("DosCloseQueue(%u)", handle);
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_QUEUE, &borrow))
        return ERROR_QUE_INVALID_HANDLE;
    Queue *queue = borrow.resource;
    lock_queues();
    APIRET result = ERROR_QUE_INVALID_HANDLE;
    if (!queue->closed)
    {
        queue->closed = TRUE;
        Queue **link = &queues;
        while (*link != queue)
            link = &(*link)->next;
        *link = queue->next;
        purge(queue);
        lx_host_monitor_wake_all(monitor);
        result = NO_ERROR;
    }
    lx_host_monitor_unlock(monitor);
    if (!result)
        lx_handle_close(handle, LX_HANDLE_QUEUE);
    lx_handle_release(&borrow);
    return result;
}
APIRET DosPurgeQueue(HQUEUE handle)
{
    TRACE_NATIVE("DosPurgeQueue(%u)", handle);
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_QUEUE, &borrow))
        return ERROR_QUE_INVALID_HANDLE;
    Queue *queue = borrow.resource;
    lock_queues();
    APIRET result = queue->closed ? ERROR_QUE_INVALID_HANDLE : NO_ERROR;
    if (!result)
        purge(queue);
    lx_host_monitor_unlock(monitor);
    lx_handle_release(&borrow);
    return result;
}
APIRET DosQueryQueue(HQUEUE handle, PULONG count)
{
    TRACE_NATIVE("DosQueryQueue(%u, %p)", handle, count);
    if (!count)
        return ERROR_INVALID_PARAMETER;
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_QUEUE, &borrow))
        return ERROR_QUE_INVALID_HANDLE;
    Queue *queue = borrow.resource;
    lock_queues();
    APIRET result = queue->closed ? ERROR_QUE_INVALID_HANDLE : NO_ERROR;
    if (!result)
        *count = queue->count;
    lx_host_monitor_unlock(monitor);
    lx_handle_release(&borrow);
    return result;
}
APIRET DosWriteQueue(HQUEUE handle, ULONG request, ULONG size, PVOID data, ULONG priority)
{
    TRACE_NATIVE("DosWriteQueue(%u, %u, %u, %p, %u)", handle, request, size, data, priority);
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_QUEUE, &borrow))
        return ERROR_QUE_INVALID_HANDLE;
    Queue *queue = borrow.resource;
    lock_queues();
    APIRET result = queue->closed ? ERROR_QUE_INVALID_HANDLE : NO_ERROR;
    QueueEntry *entry = NULL;
    if (priority > 15)
        priority = 15;
    if (!result && (queue->next_id == UINT32_MAX || !(entry = calloc(1, sizeof *entry))))
        result = ERROR_QUE_NO_MEMORY;
    if (!result)
    {
        entry->id = ++queue->next_id;
        entry->request = request;
        entry->size = size;
        entry->data = data;
        entry->priority = queue->order == QUE_PRIORITY ? priority : 0;
        entry->sender = GLoaderState.pib.pib_ulpid;
        QueueEntry **link = &queue->head;
        if (queue->order == QUE_FIFO)
            while (*link)
                link = &(*link)->next;
        else if (queue->order == QUE_PRIORITY)
            while (*link && (*link)->priority >= priority)
                link = &(*link)->next;
        entry->next = *link;
        *link = entry;
        queue->count++;
        lx_host_monitor_wake_all(monitor);
        post_event(queue->event);
    }
    lx_host_monitor_unlock(monitor);
    lx_handle_release(&borrow);
    return result;
}
typedef struct ReceiveCall
{
    LxHandleBorrow borrow;
    PREQUESTDATA request;
    PULONG size, cursor;
    PPVOID data;
    PBYTE priority;
    HEV event;
    BOOL32 nowait;
    BOOL peek;
} ReceiveCall;
static void receive_cleanup(void *argument)
{
    ReceiveCall *call = argument;
    lx_handle_release(&call->borrow);
}
static uint32 receive_operation(void *argument)
{
    ReceiveCall *call = argument;
    Queue *queue = call->borrow.resource;
    PREQUESTDATA request = call->request;
    PULONG size = call->size, cursor = call->cursor;
    PPVOID data = call->data;
    PBYTE priority = call->priority;
    HEV event = call->event;
    BOOL32 nowait = call->nowait;
    BOOL peek = call->peek;
    lock_queues();
    APIRET result = NO_ERROR;
    if (nowait)
    {
        if (queue->event && queue->event != event)
            result = ERROR_INVALID_PARAMETER;
        else
            queue->event = event;
    }
    while (!result)
    {
        if (queue->closed)
        {
            result = ERROR_QUE_INVALID_HANDLE;
            break;
        }
        QueueEntry **link = &queue->head;
        if (*cursor)
        {
            while (*link && (*link)->id != *cursor)
                link = &(*link)->next;
            if (!*link)
            {
                result = ERROR_QUE_ELEMENT_NOT_EXIST;
                break;
            }
            if (peek)
            {
                link = &(*link)->next;
                if (!*link)
                {
                    result = ERROR_QUE_PREV_AT_END;
                    break;
                }
            }
        }
        QueueEntry *entry = *link;
        if (!entry)
        {
            if (nowait)
            {
                result = ERROR_QUE_EMPTY;
                break;
            }
            if (lx_host_monitor_wait(monitor, UINT32_MAX) != LX_HOST_LOCK_OK)
            {
                result = ERROR_QUE_MEMORY_ERROR;
                break;
            }
            continue;
        }
        request->pid = entry->sender;
        request->ulData = entry->request;
        *size = entry->size;
        *data = entry->data;
        *priority = (BYTE)entry->priority;
        if (peek)
            *cursor = entry->id;
        else
        {
            *link = entry->next;
            free(entry);
            queue->count--;
        }
        break;
    }
    lx_host_monitor_unlock(monitor);
    return result;
}
static APIRET receive(HQUEUE handle, PREQUESTDATA request, PULONG size, PPVOID data, PULONG cursor,
                      BOOL32 nowait, PBYTE priority, HEV event, BOOL peek)
{
    if (!request || !size || !data || !cursor || !priority)
        return ERROR_INVALID_PARAMETER;
    if (nowait > 1)
        return ERROR_QUE_INVALID_WAIT;
    if (nowait)
    {
        LxHandleBorrow semaphore;
        if (!event || !lx_handle_borrow(event, LX_HANDLE_EVENT, &semaphore))
            return ERROR_INVALID_PARAMETER;
        lx_handle_release(&semaphore);
    }
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_QUEUE, &borrow))
        return ERROR_QUE_INVALID_HANDLE;
    ReceiveCall call = {borrow, request, size, cursor, data, priority, event, nowait, peek};
    return lx_host_guarded_call(receive_operation, &call, receive_cleanup, &call);
}
APIRET DosPeekQueue(HQUEUE handle, PREQUESTDATA request, PULONG size, PPVOID data, PULONG element,
                    BOOL32 nowait, PBYTE priority, HEV event)
{
    TRACE_NATIVE("DosPeekQueue(%u, %p, %p, %p, %p, %u, %p, %u)", handle, request, size, data,
                 element, nowait, priority, event);
    return receive(handle, request, size, data, element, nowait, priority, event, TRUE);
}
APIRET DosReadQueue(HQUEUE handle, PREQUESTDATA request, PULONG size, PPVOID data, ULONG element,
                    BOOL32 nowait, PBYTE priority, HEV event)
{
    TRACE_NATIVE("DosReadQueue(%u, %p, %p, %p, %u, %u, %p, %u)", handle, request, size, data,
                 element, nowait, priority, event);
    return receive(handle, request, size, data, &element, nowait, priority, event, FALSE);
}
#include "os2libs/generated/quecalls-lx.h"
