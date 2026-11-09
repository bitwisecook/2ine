/* Public domain. No rights reserved. Wholly AI-authored. */
/* DeferWindowPos batches: positions collected per thread and applied together by
   EndDeferWindowPos. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/user/windows/windows.h"
#include "emulator/handles.h"

typedef struct DeferredWindow
{
    ULONG window, after, flags;
    LONG x, y, w, h;
} DeferredWindow;

typedef struct DeferredBatch
{
    size_t count, capacity;
    DeferredWindow *items;
    ULONG handle;
    void *owner;
    struct DeferredBatch *next;
} DeferredBatch;

static _Thread_local DeferredBatch *deferred_batches;

static void destroy_defer(void *resource)
{
    DeferredBatch *batch = resource;
    DeferredBatch **link = &deferred_batches;
    while (*link && *link != batch)
        link = &(*link)->next;
    if (*link)
        *link = batch->next;
    free(batch->items);
    free(batch);
}

ULONG BeginDeferWindowPos(LONG count)
{
    if (count < 0 || count > 65536)
    {
        open32SetError(87);
        return 0;
    }
    DeferredBatch *batch = calloc(1, sizeof *batch);
    if (batch)
    {
        batch->capacity = count ? (size_t)count : 8;
        batch->items = calloc(batch->capacity, sizeof *batch->items);
    }
    ULONG handle =
        batch && batch->items ? lx_handle_create(LX_HANDLE_OPEN32_DEFER, batch, destroy_defer) : 0;
    if (!handle)
    {
        if (batch)
            destroy_defer(batch);
        open32SetError(8);
    }
    if (handle)
    {
        batch->handle = handle;
        batch->owner = lxHostThreadState();
        batch->next = deferred_batches;
        deferred_batches = batch;
    }
    return handle;
}

ULONG DeferWindowPos(ULONG handle, ULONG window, ULONG after, LONG x, LONG y, LONG width,
                     LONG height, ULONG flags)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DEFER, &borrow))
    {
        open32SetError(6);
        return 0;
    }
    DeferredBatch *batch = borrow.resource;
    if (batch->owner != lxHostThreadState())
    {
        lx_handle_release(&borrow);
        open32SetError(6);
        return 0;
    }
    BOOL valid = IsWindow(window);
    if (valid && batch->count == batch->capacity)
    {
        size_t capacity = batch->capacity * 2;
        DeferredWindow *items =
            capacity <= 65536 ? realloc(batch->items, capacity * sizeof *items) : NULL;
        if (!items)
            valid = FALSE;
        else
        {
            batch->items = items;
            batch->capacity = capacity;
        }
    }
    if (valid)
        batch->items[batch->count++] = (DeferredWindow){window, after, flags, x, y, width, height};
    lx_handle_release(&borrow);
    if (!valid)
    {
        lx_handle_close(handle, LX_HANDLE_OPEN32_DEFER);
        open32SetError(1400);
        return 0;
    }
    return handle;
}

BOOL EndDeferWindowPos(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_DEFER, &borrow))
    {
        open32SetError(6);
        return FALSE;
    }
    DeferredBatch *batch = borrow.resource;
    if (batch->owner != lxHostThreadState())
    {
        lx_handle_release(&borrow);
        open32SetError(6);
        return FALSE;
    }
    BOOL valid = TRUE;
    for (size_t i = 0; i < batch->count; i++)
        if (!IsWindow(batch->items[i].window))
            valid = FALSE;
    if (valid)
        for (size_t i = 0; i < batch->count; i++)
        {
            DeferredWindow *w = batch->items + i;
            if (!SetWindowPos(w->window, w->after, w->x, w->y, w->w, w->h, w->flags))
            {
                valid = FALSE;
                break;
            }
        }
    lx_handle_release(&borrow);
    lx_handle_close(handle, LX_HANDLE_OPEN32_DEFER);
    return valid;
}

void open32DeferredCleanup(void)
{
    while (deferred_batches)
    {
        DeferredBatch *batch = deferred_batches;
        deferred_batches = batch->next;
        lx_handle_close(batch->handle, LX_HANDLE_OPEN32_DEFER);
    }
}
