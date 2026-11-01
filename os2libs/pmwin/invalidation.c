/* Public domain. No rights reserved. Wholly AI-authored. */
/* Damage persists until validated; the message loop synthesizes paint requests. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/paint.h"
static void remove_paint(AnchorBlock *anchor, HWND handle)
{
    MessageQueueItem *previous = NULL, *item = anchor->message_queue.head;
    while (item)
    {
        MessageQueueItem *next = item->next;
        if (item->qmsg.hwnd == handle && item->qmsg.msg == WM_PAINT)
        {
            if (previous)
                previous->next = next;
            else
                anchor->message_queue.head = next;
            if (anchor->message_queue.tail == item)
                anchor->message_queue.tail = previous;
            item->next = anchor->message_queue.free_pool;
            anchor->message_queue.free_pool = item;
        }
        else
            previous = item;
        item = next;
    }
}
void validateWindowPaint(AnchorBlock *anchor, Window *window)
{
    window->invalid = (RECTL){0, 0, 0, 0};
    window->paint_pending = FALSE;
    remove_paint(anchor, window->hwnd);
}
BOOL windowAcceptsUpdates(const Window *window)
{
    for (; window && window != &desktop_window; window = window->parent)
        if (!(window->style & WS_VISIBLE))
            return FALSE;
    return TRUE;
}
static void suspend_paint(AnchorBlock *anchor, Window *window)
{
    window->paint_pending = FALSE;
    remove_paint(anchor, window->hwnd);
    for (Window *child = window->children; child; child = child->sibling)
        suspend_paint(anchor, child);
}
BOOL WinEnableWindowUpdate(HWND handle, BOOL enable)
{
    TRACE_NATIVE("WinEnableWindowUpdate(%u, %u)", (uint)handle, (uint)enable);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window)
        return FALSE;
    if (enable != FALSE && enable != TRUE)
    {
        lxHostThreadState()->pm_error = PMERR_INVALID_FLAG;
        return FALSE;
    }
    BOOL was_visible = (window->style & WS_VISIBLE) != 0;
    if (enable)
        window->style |= WS_VISIBLE;
    else
        window->style &= ~WS_VISIBLE;
    if (was_visible != enable)
        sendMessage(window, WM_SHOW, (MPARAM)(uintptr_t)enable, 0);
    if (enable)
        return TRUE;
    /* Descendants retain their damage until updates are enabled again. */
    suspend_paint(anchor, window);
    return TRUE;
}
BOOL WinInvalidateRect(HWND handle, PRECTL rectangle, BOOL children)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window)
        return FALSE;
    RECTL bounds = {0, 0, window->w, window->h}, damaged;
    if (!WinIntersectRect(0, &damaged, &bounds, rectangle ? rectangle : &bounds))
        return TRUE;
    WinUnionRect(0, &window->invalid, &window->invalid, &damaged);
    if (windowAcceptsUpdates(window))
        window->paint_pending = TRUE;
    if (children)
        for (Window *child = window->children; child; child = child->sibling)
        {
            RECTL local = damaged;
            WinOffsetRect(0, &local, -child->x, -child->y);
            WinInvalidateRect(child->hwnd, &local, TRUE);
        }
    return TRUE;
}
BOOL WinQueryUpdateRect(HWND handle, PRECTL rect)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window || !rect)
        return FALSE;
    *rect = window->invalid;
    return !WinIsRectEmpty(0, rect);
}
BOOL WinUpdateWindow(HWND handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window)
        return FALSE;
    if (!WinIsRectEmpty(0, &window->invalid) && WinIsWindowVisible(handle) &&
        windowAcceptsUpdates(window))
    {
        remove_paint(anchor, handle);
        sendMessage(window, WM_PAINT, 0, 0);
    }
    return TRUE;
}
HWND WinWindowFromDC(HDC dc)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    return anchor && getWindowFromHWND(anchor, dc) ? dc : 0;
}

BOOL lx_pm_validate_rect(HWND handle, const RECTL *rectangle, BOOL internal_only)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    if (!window)
        return FALSE;
    const RECTL *old = &window->invalid;
    if (internal_only)
    {
        if (WinIsRectEmpty(0, old))
            validateWindowPaint(anchor, window);
    }
    else if (!rectangle || (rectangle->xLeft <= old->xLeft && rectangle->yBottom <= old->yBottom &&
                            rectangle->xRight >= old->xRight && rectangle->yTop >= old->yTop))
        validateWindowPaint(anchor, window);
    return TRUE;
}
