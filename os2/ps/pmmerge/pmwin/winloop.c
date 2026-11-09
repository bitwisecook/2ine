/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* The message loop: WinGetMsg, WinPeekMsg and WinDispatchMsg, with WM_PAINT
   synthesis from window damage and host event pumping. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmwin/input.h"
#include "os2/ps/pmmerge/pmgre/graphics.h"
#include "os2/ps/pmmerge/pmwin/menus.h"
#include "os2/ps/pmmerge/pmwin/open32_post.h"
#include "os2/include/private/pmp.h"
#include "emulator/launch_record.h"
#include "os2/ps/pmmerge/pmwin/paint.h"
#include "emulator/abi.h"
#include <ctype.h>

/* Paint parents before children and lower siblings before overlapping upper siblings.
   Retrieving WM_PAINT never validates damage: an application may defer its drawing. */
static Window *nextPaintSibling(AnchorBlock *anchor, Window *window, HWND filter, ULONG first,
                                ULONG last, LxPmPaintFilter paint_filter, void *context)
{
    if (!window)
        return NULL;
    Window *found =
        nextPaintSibling(anchor, window->sibling, filter, first, last, paint_filter, context);
    if (found)
        return found;
    if (getWindowFromHWND(anchor, window->hwnd) != window || !(window->style & WS_VISIBLE) ||
        window->w <= 0 || window->h <= 0)
        return NULL;
    QMSG message = {.hwnd = window->hwnd, .msg = WM_PAINT};
    if (window->paint_pending && !WinIsRectEmpty(0, &window->invalid) &&
        !skipMessage(&message, filter, first, last) &&
        (!paint_filter || paint_filter(window->hwnd, context)))
        return window;
    return nextPaintSibling(anchor, window->children, filter, first, last, paint_filter, context);
}

static BOOL filteredPaintMessage(AnchorBlock *anchor, PQMSG output, HWND filter, ULONG first,
                                 ULONG last, LxPmPaintFilter paint_filter, void *context)
{
    Window *window = nextPaintSibling(anchor, desktop_window.children, filter, first, last,
                                      paint_filter, context);
    if (!window || getWindowFromHWND(anchor, window->hwnd) != window)
        return FALSE;
    /* Do not expose a cleared parent before its children have had a paint
       request. Keep the timed fallback only for damage the app has deferred. */
    window->paint_offered = TRUE;
    *output = (QMSG){.hwnd = window->hwnd, .msg = WM_PAINT, .time = currentSystemTicks()};
    return TRUE;
}

static MessageQueueItem *queuedMessage(AnchorBlock *anchor, MessageQueueItem **previous,
                                       HWND filter, ULONG first, ULONG last)
{
    MessageQueueItem *timer = NULL, *timer_previous = NULL, *prev = NULL;
    for (MessageQueueItem *item = anchor->message_queue.head; item; prev = item, item = item->next)
    {
        if (skipMessage(&item->qmsg, filter, first, last))
            continue;
        if (item->qmsg.msg != WM_TIMER)
        {
            *previous = prev;
            return item;
        }
        if (!timer)
        {
            timer = item;
            timer_previous = prev;
        }
    }
    *previous = timer_previous;
    return timer;
}

static void pumpFilteredEvents(AnchorBlock *anchor, LxPmPaintFilter paint_filter, void *context)
{
    LxHostEvent event;
    while (!anchor->message_queue.head)
    {
        lx_execution_poll_termination(GProcessState.execution);
        drainForeignMessages(anchor);
        lx_pm_dive_pump(anchor);
        lx_pm_video_pump(anchor);
        presentWindows(anchor);
        while (lx_host_video_poll(&event))
            lx_pm_host_event(&event);
        int wait = pumpTimers(anchor);
        if (wait < 0 || wait > 10)
            wait = 10; /* Cross-process posts arrive through the ball's mailbox. */
        QMSG paint;
        if (anchor->message_queue.head ||
            filteredPaintMessage(anchor, &paint, 0, 0, 0, paint_filter, context))
            break;
        if (lx_host_video_wait_timeout(&event, wait))
            lx_pm_host_event(&event);
    }
}

void pumpEvents(AnchorBlock *anchor)
{
    pumpFilteredEvents(anchor, NULL, NULL);
}

BOOL skipMessage(const PQMSG qmsg, HWND hwndFilter, ULONG msgFilterFirst, ULONG msgFilterLast)
{
    if (hwndFilter && (qmsg->hwnd != hwndFilter))
    {
        return TRUE;
    }
    else if (msgFilterFirst || msgFilterLast)
    {
        const ULONG msg = qmsg->msg;
        if (msgFilterFirst > msgFilterLast)
        { // reject if between first and last.
            if ((msg >= msgFilterLast) && (msg <= msgFilterFirst))
                return TRUE;
        }
        else
        { // reject if not between first and last.
            if ((msg < msgFilterFirst) || (msg > msgFilterLast))
                return TRUE;
        }
    }
    return FALSE;
}

static BOOL getMessage(HAB hab, PQMSG pqmsg, HWND hwndFilter, ULONG msgFilterFirst,
                       ULONG msgFilterLast, LxPmPaintFilter paint_filter, void *context)
{
    TRACE_NATIVE("WinGetMsg(%u, %p, %u, %u, %u)", (uint)hab, pqmsg, (uint)hwndFilter,
                 (uint)msgFilterFirst, (uint)msgFilterLast);
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor)
    {
        return FALSE;
    }
    else if (hwndFilter)
    {
        FIXME("fail with PMERR_INVALID_HWND if this is a bogus HWND.");
    }

    while (1)
    {
        /* Present retained drawing even while queued timers keep the queue busy. */
        drainForeignMessages(anchor);
        lx_pm_dive_pump(anchor);
        lx_pm_video_pump(anchor);
        LxHostEvent event;
        while (lx_host_video_poll(&event))
            lx_pm_host_event(&event);
        presentWindows(anchor);
        pumpTimers(anchor);
        MessageQueueItem *prev = NULL;
        MessageQueueItem *i =
            queuedMessage(anchor, &prev, hwndFilter, msgFilterFirst, msgFilterLast);
        if ((!i || i->qmsg.msg == WM_TIMER) &&
            filteredPaintMessage(anchor, pqmsg, hwndFilter, msgFilterFirst, msgFilterLast,
                                 paint_filter, context))
            return TRUE;

        if (i != NULL)
        {
            memcpy(pqmsg, &i->qmsg, sizeof(QMSG));

            TRACE_EVENT(
                "EVENT GET { hwnd=%u, msg=%u (%s), mp1=%p, mp2=%p, time=%u, ptl={%d,%d}, reserved=%u }",
                (uint)pqmsg->hwnd, (uint)pqmsg->msg, messageName(pqmsg->msg), pqmsg->mp1,
                pqmsg->mp2, (uint)pqmsg->time, (int)pqmsg->ptl.x, (int)pqmsg->ptl.y,
                (uint)pqmsg->reserved);

            if (prev)
            {
                prev->next = i->next;
            }
            else
            {
                anchor->message_queue.head = i->next;
            }

            if (anchor->message_queue.tail == i)
                anchor->message_queue.tail = prev;

            i->next = anchor->message_queue.free_pool;
            anchor->message_queue.free_pool = i;

            anchor->message_queue.last_position = pqmsg->ptl;
            synchronizeInputState(anchor, pqmsg);
            if (filterInputMessage(anchor, pqmsg, 1))
                continue;
            return (pqmsg->msg != WM_QUIT) ? TRUE : FALSE;
        }

        /* An empty queue is the idle state a launcher waits for. */
        lx_launch_mark_input_idle();
        pumpFilteredEvents(anchor, paint_filter, context); // may block.
    }

    assert(!"shouldn't hit this.");
    return TRUE;
}

BOOL WinGetMsg(HAB hab, PQMSG output, HWND filter, ULONG first, ULONG last)
{
    return getMessage(hab, output, filter, first, last, NULL, NULL);
}

BOOL lx_pm_get_open32_message(HAB hab, PQMSG output, LxPmPaintFilter paint_filter, void *context)
{
    return getMessage(hab, output, 0, 0, 0, paint_filter, context);
}

MRESULT WinDispatchMsg(HAB hab, PQMSG pqmsg)
{
    TRACE_NATIVE("WinDispatchMsg(%u, %p)", (uint)hab, pqmsg);
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (anchor)
    {
        Window *win = getWindowFromHWND(anchor, pqmsg->hwnd);
        if (win)
        {
            return sendMessage(win, pqmsg->msg, pqmsg->mp1, pqmsg->mp2);
        }
    }
    return 0;
}

static BOOL peekMessage(HAB hab, PQMSG output, HWND filter, ULONG first, ULONG last, ULONG flags,
                        LxPmPaintFilter paint_filter, void *context)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || !output || flags > 1 || (filter && !getWindowFromHWND(anchor, filter)))
        return FALSE;
    drainForeignMessages(anchor);
    lx_pm_dive_pump(anchor);
    lx_pm_video_pump(anchor);
    LxHostEvent event;
    while (lx_host_video_poll(&event))
        lx_pm_host_event(&event);
    pumpTimers(anchor);
    presentWindows(anchor);
retry:;
    MessageQueueItem *previous = NULL;
    MessageQueueItem *item = queuedMessage(anchor, &previous, filter, first, last);
    if ((!item || item->qmsg.msg == WM_TIMER) &&
        filteredPaintMessage(anchor, output, filter, first, last, paint_filter, context))
        return TRUE;
    if (!item)
    {
        lx_launch_mark_input_idle();
        return FALSE;
    }
    *output = item->qmsg;
    anchor->message_queue.last_position = output->ptl;
    QMSG original = item->qmsg;
    if (flags & 1)
    {
        if (previous)
            previous->next = item->next;
        else
            anchor->message_queue.head = item->next;
        if (anchor->message_queue.tail == item)
            anchor->message_queue.tail = previous;
        item->next = anchor->message_queue.free_pool;
        anchor->message_queue.free_pool = item;
    }
    synchronizeInputState(anchor, output);
    if (filterInputMessage(anchor, output, flags))
    {
        if (!(flags & 1))
        {
            MessageQueueItem **link = &anchor->message_queue.head;
            previous = NULL;
            while (*link && *link != item)
            {
                previous = *link;
                link = &(*link)->next;
            }
            if (*link && !memcmp(&item->qmsg, &original, sizeof original))
            {
                *link = item->next;
                if (anchor->message_queue.tail == item)
                    anchor->message_queue.tail = previous;
                item->next = anchor->message_queue.free_pool;
                anchor->message_queue.free_pool = item;
            }
        }
        goto retry;
    }
    return TRUE;
}

BOOL WinPeekMsg(HAB hab, PQMSG output, HWND filter, ULONG first, ULONG last, ULONG flags)
{
    return peekMessage(hab, output, filter, first, last, flags, NULL, NULL);
}

BOOL lx_pm_peek_open32_message(HAB hab, PQMSG output, LxPmPaintFilter paint_filter, void *context)
{
    return peekMessage(hab, output, 0, 0, 0, 1, paint_filter, context);
}
