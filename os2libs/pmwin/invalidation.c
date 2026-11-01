/* Public domain. No rights reserved. Wholly AI-authored. */
/* Invalidations accumulate before painting; one queued paint represents their union. */
#include "os2libs/pmwin/internal.h"
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
    remove_paint(anchor, window->hwnd);
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
    if (WinIsWindowVisible(handle))
    {
        MessageQueueItem *item = anchor->message_queue.head;
        while (item && (item->qmsg.hwnd != handle || item->qmsg.msg != WM_PAINT))
            item = item->next;
        if (!item && !postTimestampedMsg(anchor, handle, WM_PAINT, 0, 0, currentSystemTicks()))
            return FALSE;
    }
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
    if (!WinIsRectEmpty(0, &window->invalid) && WinIsWindowVisible(handle))
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
