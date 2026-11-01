/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
const char *messageName(const ULONG msg)
{
    switch (msg)
    {
#define MSGCASE(m)                                                                                 \
    case m:                                                                                        \
        return #m
        MSGCASE(WM_NULL);
        MSGCASE(WM_CREATE);
        MSGCASE(WM_DESTROY);
        MSGCASE(WM_ENABLE);
        MSGCASE(WM_SHOW);
        MSGCASE(WM_MOVE);
        MSGCASE(WM_SIZE);
        MSGCASE(WM_ADJUSTWINDOWPOS);
        MSGCASE(WM_CALCVALIDRECTS);
        MSGCASE(WM_SETWINDOWPARAMS);
        MSGCASE(WM_QUERYWINDOWPARAMS);
        MSGCASE(WM_HITTEST);
        MSGCASE(WM_ACTIVATE);
        MSGCASE(WM_SETFOCUS);
        MSGCASE(WM_SETSELECTION);
        MSGCASE(WM_PPAINT);
        MSGCASE(WM_PSETFOCUS);
        MSGCASE(WM_PSYSCOLORCHANGE);
        MSGCASE(WM_PSIZE);
        MSGCASE(WM_PACTIVATE);
        MSGCASE(WM_PCONTROL);
        MSGCASE(WM_COMMAND);
        MSGCASE(WM_SYSCOMMAND);
        MSGCASE(WM_HELP);
        MSGCASE(WM_PAINT);
        MSGCASE(WM_TIMER);
        MSGCASE(WM_SEM1);
        MSGCASE(WM_SEM2);
        MSGCASE(WM_SEM3);
        MSGCASE(WM_SEM4);
        MSGCASE(WM_CLOSE);
        MSGCASE(WM_QUIT);
        MSGCASE(WM_SYSCOLORCHANGE);
        MSGCASE(WM_SYSVALUECHANGED);
        MSGCASE(WM_APPTERMINATENOTIFY);
        MSGCASE(WM_PRESPARAMCHANGED);
        MSGCASE(WM_CONTROL);
        MSGCASE(WM_VSCROLL);
        MSGCASE(WM_HSCROLL);
        MSGCASE(WM_INITMENU);
        MSGCASE(WM_MENUSELECT);
        MSGCASE(WM_MENUEND);
        MSGCASE(WM_DRAWITEM);
        MSGCASE(WM_MEASUREITEM);
        MSGCASE(WM_CONTROLPOINTER);
        MSGCASE(WM_QUERYDLGCODE);
        MSGCASE(WM_INITDLG);
        MSGCASE(WM_SUBSTITUTESTRING);
        MSGCASE(WM_MATCHMNEMONIC);
        MSGCASE(WM_SAVEAPPLICATION);
        MSGCASE(WM_FLASHWINDOW);
        MSGCASE(WM_FORMATFRAME);
        MSGCASE(WM_UPDATEFRAME);
        MSGCASE(WM_FOCUSCHANGE);
        MSGCASE(WM_SETBORDERSIZE);
        MSGCASE(WM_TRACKFRAME);
        MSGCASE(WM_MINMAXFRAME);
        MSGCASE(WM_SETICON);
        MSGCASE(WM_QUERYICON);
        MSGCASE(WM_SETACCELTABLE);
        MSGCASE(WM_QUERYACCELTABLE);
        MSGCASE(WM_TRANSLATEACCEL);
        MSGCASE(WM_QUERYTRACKINFO);
        MSGCASE(WM_QUERYBORDERSIZE);
        MSGCASE(WM_NEXTMENU);
        MSGCASE(WM_ERASEBACKGROUND);
        MSGCASE(WM_QUERYFRAMEINFO);
        MSGCASE(WM_QUERYFOCUSCHAIN);
        MSGCASE(WM_OWNERPOSCHANGE);
        MSGCASE(WM_CALCFRAMERECT);
        MSGCASE(WM_WINDOWPOSCHANGED);
        MSGCASE(WM_ADJUSTFRAMEPOS);
        MSGCASE(WM_QUERYFRAMECTLCOUNT);
        MSGCASE(WM_QUERYHELPINFO);
        MSGCASE(WM_SETHELPINFO);
        MSGCASE(WM_ERROR);
        MSGCASE(WM_REALIZEPALETTE);
        MSGCASE(WM_RENDERFMT);
        MSGCASE(WM_RENDERALLFMTS);
        MSGCASE(WM_DESTROYCLIPBOARD);
        MSGCASE(WM_PAINTCLIPBOARD);
        MSGCASE(WM_SIZECLIPBOARD);
        MSGCASE(WM_HSCROLLCLIPBOARD);
        MSGCASE(WM_VSCROLLCLIPBOARD);
        MSGCASE(WM_DRAWCLIPBOARD);
        MSGCASE(WM_MOUSEMOVE);
        MSGCASE(WM_BUTTON1DOWN);
        MSGCASE(WM_BUTTON1UP);
        MSGCASE(WM_BUTTON1DBLCLK);
        MSGCASE(WM_BUTTON2DOWN);
        MSGCASE(WM_BUTTON2UP);
        MSGCASE(WM_BUTTON2DBLCLK);
        MSGCASE(WM_BUTTON3DOWN);
        MSGCASE(WM_BUTTON3UP);
        MSGCASE(WM_BUTTON3DBLCLK);
        MSGCASE(WM_CHAR);
        MSGCASE(WM_VIOCHAR);
        MSGCASE(WM_JOURNALNOTIFY);
        MSGCASE(WM_MOUSEMAP);
        MSGCASE(WM_VRNDISABLED);
        MSGCASE(WM_VRNENABLED);
        MSGCASE(WM_DDE_INITIATE);
        MSGCASE(WM_DDE_REQUEST);
        MSGCASE(WM_DDE_ACK);
        MSGCASE(WM_DDE_DATA);
        MSGCASE(WM_DDE_ADVISE);
        MSGCASE(WM_DDE_UNADVISE);
        MSGCASE(WM_DDE_POKE);
        MSGCASE(WM_DDE_EXECUTE);
        MSGCASE(WM_DDE_TERMINATE);
        MSGCASE(WM_DDE_INITIATEACK);
        MSGCASE(WM_QUERYCONVERTPOS);
        MSGCASE(WM_MSGBOXINIT);
        MSGCASE(WM_MSGBOXDISMISS);
        MSGCASE(WM_CTLCOLORCHANGE);
        MSGCASE(WM_QUERYCTLTYPE);
        MSGCASE(WM_CHORD);
        MSGCASE(WM_BUTTON1MOTIONSTART);
        MSGCASE(WM_BUTTON1MOTIONEND);
        MSGCASE(WM_BUTTON1CLICK);
        MSGCASE(WM_BUTTON2MOTIONSTART);
        MSGCASE(WM_BUTTON2MOTIONEND);
        MSGCASE(WM_BUTTON2CLICK);
        MSGCASE(WM_BUTTON3MOTIONSTART);
        MSGCASE(WM_BUTTON3MOTIONEND);
        MSGCASE(WM_BUTTON3CLICK);
        MSGCASE(WM_BEGINDRAG);
        MSGCASE(WM_ENDDRAG);
        MSGCASE(WM_SINGLESELECT);
        MSGCASE(WM_OPEN);
        MSGCASE(WM_CONTEXTMENU);
        MSGCASE(WM_CONTEXTHELP);
        MSGCASE(WM_TEXTEDIT);
        MSGCASE(WM_BEGINSELECT);
        MSGCASE(WM_ENDSELECT);
        MSGCASE(WM_PICKUP);
        MSGCASE(WM_SEMANTICEVENT);
        MSGCASE(WM_USER);
#undef MSGCASE
        default:
            break;
    }

    return "???";
}

MRESULT sendMessage(Window *win, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    PFNWP winproc = win->window_proc;
    if (!winproc)
    {
        winproc = WinDefWindowProc;
    }
    TRACE_EVENT("EVENT SEND { hwnd=%u, msg=%u (%s), mp1=%p, mp2=%p, proc=%p }", (uint)win->hwnd,
                (uint)msg, messageName(msg), mp1, mp2, winproc);
    if (GLoaderState.execution && (uintptr_t)winproc <= UINT32_MAX &&
        lx_execution_memory(GLoaderState.execution, (uint32_t)(uintptr_t)winproc, 1,
                            LX_GUEST_EXECUTE))
        return (MRESULT)(uintptr_t)lx_execution_callback32_4(
            GLoaderState.execution, (uint32_t)(uintptr_t)winproc, win->hwnd, msg,
            (uint32_t)(uintptr_t)mp1, (uint32_t)(uintptr_t)mp2);
    return winproc(win->hwnd, msg, mp1, mp2);
}

BOOL destroyMessageQueue(AnchorBlock *anchor)
{
    MessageQueueItem *next = NULL;
    for (MessageQueueItem *i = anchor->message_queue.head; i; i = next)
    {
        next = i->next;
        free(i);
    }
    for (MessageQueueItem *i = anchor->message_queue.free_pool; i; i = next)
    {
        next = i->next;
        free(i);
    }
    TRACE_EVENT("HMQ %u destroyed", (uint)anchor->message_queue.hmq);
    memset(&anchor->message_queue, '\0', sizeof(MessageQueue));
    return TRUE;
}

HMQ WinCreateMsgQueue(HAB hab, LONG cmsg)
{
    TRACE_NATIVE("WinCreateMsgQueue(%u, %d)", (uint)hab, (int)cmsg);
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor)
    {
        return NULLHANDLE;
    }
    else if (anchor->message_queue.hmq != NULLHANDLE)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_MSG_QUEUE_ALREADY_EXISTS, NULLHANDLE);
    }

    /* Message queues grow dynamically; cmsg does not limit capacity. */

    assert(anchor->message_queue.head == NULL);
    assert(anchor->message_queue.tail == NULL);
    assert(anchor->message_queue.free_pool == NULL);

    const int ihmq = lx_host_atomic_increment(&GHMQCounter);
    if (ihmq <= 0)
    {
        return NULLHANDLE;
    }

    const HMQ hmq = (HMQ)ihmq;
    anchor->message_queue.hmq = hmq;
    TRACE_EVENT("HMQ %u created", (uint)hmq);
    return hmq;
}

ERRORID postMessage(AnchorBlock *anchor, const QMSG *qmsg)
{
    TRACE_EVENT(
        "EVENT POST { hwnd=%u, msg=%u (%s), mp1=%p, mp2=%p, time=%u, ptl={%d,%d}, reserved=%u }",
        (uint)qmsg->hwnd, (uint)qmsg->msg, messageName(qmsg->msg), qmsg->mp1, qmsg->mp2,
        (uint)qmsg->time, (int)qmsg->ptl.x, (int)qmsg->ptl.y, (uint)qmsg->reserved);

    MessageQueueItem *item = anchor->message_queue.free_pool;
    if (item)
    {
        anchor->message_queue.free_pool = item->next;
    }
    else
    {
        item = (MessageQueueItem *)malloc(sizeof(*item));
        if (item == NULL)
        {
            return PMERR_HEAP_OUT_OF_MEMORY;
        }
    }

    memcpy(&item->qmsg, qmsg, sizeof(*qmsg));
    item->next = NULL;

    assert(!anchor->message_queue.head == !anchor->message_queue.tail);
    if (anchor->message_queue.tail)
    {
        anchor->message_queue.tail->next = item;
    }
    else
    {
        anchor->message_queue.head = item;
    }
    anchor->message_queue.tail = item;

    return NO_ERROR;
}

void discardWindowMessages(AnchorBlock *anchor, HWND window)
{
    MessageQueueItem *previous = NULL, *item = anchor->message_queue.head;
    while (item)
    {
        MessageQueueItem *next = item->next;
        if (item->qmsg.hwnd == window)
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

BOOL postTimestampedMsg(AnchorBlock *anchor, HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2,
                        ULONG ticks)
{
    const QMSG qmsg = {hwnd, msg, mp1, mp2, ticks, {0, 0}, 0};
    return postMessage(anchor, &qmsg) == NO_ERROR;
}

BOOL postExposedMessages(AnchorBlock *anchor, Window *win, const ULONG ticks)
{
    FIXME("this should WinInvalidateRect() the windows, not send paint messages");
    FIXME(
        "this is a recursive hell"); // Sibling traversal starts at the bottom of the window stack.
    BOOL retval = FALSE;
    if (win)
    {
        retval |= postExposedMessages(anchor, win->sibling, ticks);
        if (win->style & WS_VISIBLE)
        {
            FIXME(
                "for CS_SYNCPAINT classes, this should send the WM_PAINT message right now instead of posting");
            BOOL visible = win->w > 0 && win->h > 0;
            for (Window *parent = win->parent; visible && parent && parent != &desktop_window;
                 parent = parent->parent)
                visible = (parent->style & WS_VISIBLE) != 0;
            if (visible)
                retval |= WinInvalidateRect(win->hwnd, NULL, FALSE);
            retval |= postExposedMessages(anchor, win->children, ticks);
        }
    }
    return retval;
}

BOOL processHostEvent(AnchorBlock *anchor, const LxHostEvent *event)
{
    ULONG ticks = (ULONG)event->timestamp;
    Window *win = event->window_data;
    if (event->type == LX_HOST_EVENT_QUIT)
        return postTimestampedMsg(anchor, 0, WM_QUIT, 0, 0, ticks);
    if (!win || !win->hwnd)
        return FALSE;
    if (event->type == LX_HOST_EVENT_KEY_DOWN || event->type == LX_HOST_EVENT_KEY_UP ||
        event->type == LX_HOST_EVENT_TEXT)
        return processKeyboardEvent(anchor, event);
    if (event->type == LX_HOST_EVENT_FOCUS_GAINED)
    {
        WinSetActiveWindow(HWND_DESKTOP, win->hwnd);
        return FALSE;
    }
    if (event->type == LX_HOST_EVENT_FOCUS_LOST)
    {
        if (topLevelWindow(anchor, anchor->focus) == win->hwnd)
            WinSetFocus(HWND_DESKTOP, 0);
        return FALSE;
    }
    if (event->type == LX_HOST_EVENT_CLOSE)
        return postTimestampedMsg(anchor, win->hwnd, WM_CLOSE, 0, 0, ticks);
    if (event->type == LX_HOST_EVENT_SHOWN || event->type == LX_HOST_EVENT_EXPOSED)
        return postExposedMessages(anchor, win, ticks);
    if (event->type == LX_HOST_EVENT_RESIZED)
    {
        if (event->width == win->w && event->height == win->h)
            return FALSE;
        return WinSetWindowPos(win->hwnd, HWND_TOP, win->x, win->y, event->width, event->height,
                               SWP_SIZE | SWP_NOADJUST);
    }
    if (event->type != LX_HOST_EVENT_MOTION && event->type != LX_HOST_EVENT_BUTTON_DOWN &&
        event->type != LX_HOST_EVENT_BUTTON_UP)
        return FALSE;
    for (Window *parent = win; parent && parent != &desktop_window; parent = parent->parent)
        if (!parent->enabled)
            return FALSE;
    int x = event->x, y = win->h - 1 - event->y;
    for (;;)
    {
        Window *child;
        for (child = win->children; child; child = child->sibling)
            if ((child->style & WS_VISIBLE) && x >= child->x && y >= child->y &&
                x < child->x + child->w && y < child->y + child->h)
                break;
        if (!child)
            break;
        x -= child->x;
        y -= child->y;
        win = child;
    }
    if (!win->enabled)
        return FALSE;
    ULONG message = WM_MOUSEMOVE;
    if (event->type != LX_HOST_EVENT_MOTION)
    {
        if (!event->button)
            return FALSE;
        message =
            WM_BUTTON1DOWN + (event->button - 1) * 3 + (event->type == LX_HOST_EVENT_BUTTON_UP);
    }
    MPARAM point = (MPARAM)(uintptr_t)(((uint32)(uint16)y << 16) | (uint16)x);
    BOOL result = postTimestampedMsg(anchor, win->hwnd, message, point, 0, ticks);
    if (event->type == LX_HOST_EVENT_BUTTON_UP)
        result |= postTimestampedMsg(anchor, win->hwnd, WM_BUTTON1CLICK + (event->button - 1) * 3,
                                     point, 0, ticks);
    return result;
}

static void presentWindows(AnchorBlock *anchor)
{
    for (size_t i = 0; i < anchor->windows_array_len; i++)
    {
        Window *win = anchor->windows[i];
        if (win && win->hwnd && win->parent == &desktop_window && (win->style & WS_VISIBLE))
            lx_host_window_present(win->heavy.window);
    }
}
void pumpEvents(AnchorBlock *anchor)
{
    LxHostEvent event;
    while (!anchor->message_queue.head)
    {
        drainForeignMessages(anchor);
        lx_pm_dive_pump(anchor);
        presentWindows(anchor);
        while (lx_host_video_poll(&event))
            processHostEvent(anchor, &event);
        int wait = pumpTimers(anchor);
        if (anchor->message_queue.head)
            break;
        if (lx_host_video_wait_timeout(&event, wait))
            processHostEvent(anchor, &event);
    }
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

BOOL WinGetMsg(HAB hab, PQMSG pqmsg, HWND hwndFilter, ULONG msgFilterFirst, ULONG msgFilterLast)
{
    TRACE_NATIVE("WinGetMsg(%u, %p, %u, %u, %u)", (uint)hab, pqmsg, (uint)hwndFilter,
                 (uint)msgFilterFirst, (uint)msgFilterLast);
    const BOOL bIsFiltering = (hwndFilter || msgFilterFirst || msgFilterLast);
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
        presentWindows(anchor);
        pumpTimers(anchor);
        MessageQueueItem *prev = NULL;
        MessageQueueItem *i = anchor->message_queue.head;
        if (bIsFiltering)
        {
            for (; i != NULL; prev = i, i = i->next)
            {
                if (!skipMessage(&i->qmsg, hwndFilter, msgFilterFirst, msgFilterLast))
                {
                    break;
                }
            }
        }

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

            synchronizeInputState(anchor, pqmsg);
            if (filterInputMessage(anchor, pqmsg, 1))
                continue;
            return (pqmsg->msg != WM_QUIT) ? TRUE : FALSE;
        }

        pumpEvents(anchor); // may block.
    }

    assert(!"shouldn't hit this.");
    return TRUE;
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

BOOL WinDestroyMsgQueue(HMQ hmq)
{
    TRACE_NATIVE("WinDestroyMsgQueue(%u)", (uint)hmq);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
    {
        return FALSE;
    }
    else if ((hmq == NULLHANDLE) || (anchor->message_queue.hmq != hmq))
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HMQ, FALSE);
    }

    return destroyMessageQueue(anchor);
}

MRESULT WinSendMsg(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{

    TRACE_NATIVE("WinSendMsg(%u, %u, %p, %p)", (uint)hwnd, (uint)msg, mp1, mp2);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (anchor)
    {
        Window *win = getWindowFromHWND(anchor, hwnd);
        if (win)
        {
            return sendMessage(win, msg, mp1, mp2);
        }
    }
    return (MRESULT)0;
}

BOOL WinPostMsg(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{

    TRACE_NATIVE("WinPostMsg(%u, %u, %p, %p)", (uint)hwnd, (uint)msg, mp1, mp2);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
    {
        return postForeignMessage(hwnd, msg, mp1, mp2);
    }
    if (anchor->message_queue.hmq == NULLHANDLE)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_NO_MSG_QUEUE, FALSE);
    }

    Window *win = getWindowFromHWND(anchor, hwnd);
    if (hwnd && !win)
    {
        if (postForeignMessage(hwnd, msg, mp1, mp2))
            return TRUE;
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, FALSE);
    }

    int mousex, mousey;
    lx_host_mouse_position(&mousex, &mousey);
    FIXME("track this ourselves?");
    const QMSG qmsg = {hwnd, msg, mp1, mp2, currentSystemTicks(), {mousex, mousey}, 0};
    const ERRORID rc = postMessage(anchor, &qmsg);
    if (rc)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, rc, FALSE);
    }

    return TRUE;
}

BOOL WinPostQueueMsg(HMQ hmq, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    TRACE_NATIVE("WinPostMsgQueue(%u, %u, %p, %p)", (uint)hmq, (uint)msg, mp1, mp2);
    /* Only the calling PM thread's queue is addressable by this registry. */
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || !hmq || anchor->message_queue.hmq != hmq)
        return FALSE;
    return WinPostMsg(0, msg, mp1, mp2);
}

MRESULT sendWindowPos(Window *win, ULONG msg, SWP *position)
{
    if (!TWOINE_PORTABLE_LOADER || !GLoaderState.execution)
        return sendMessage(win, msg, position, 0);
    LxGuestMapping *mapping = lx_execution_map(GLoaderState.execution, 0, 4096, 3, 0);
    if (!mapping)
        return 0;
    memcpy(lx_execution_mapping_host(mapping), position, sizeof *position);
    MRESULT result =
        sendMessage(win, msg, (MPARAM)(uintptr_t)lx_execution_mapping_address(mapping), 0);
    memcpy(position, lx_execution_mapping_host(mapping), sizeof *position);
    lx_execution_unmap(GLoaderState.execution, mapping);
    return result;
}

BOOL WinPeekMsg(HAB hab, PQMSG output, HWND filter, ULONG first, ULONG last, ULONG flags)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || !output || flags > 1 || (filter && !getWindowFromHWND(anchor, filter)))
        return FALSE;
    drainForeignMessages(anchor);
    lx_pm_dive_pump(anchor);
    LxHostEvent event;
    while (lx_host_video_poll(&event))
        processHostEvent(anchor, &event);
    pumpTimers(anchor);
    presentWindows(anchor);
retry:;
    MessageQueueItem *previous = NULL, *item = anchor->message_queue.head;
    while (item && skipMessage(&item->qmsg, filter, first, last))
    {
        previous = item;
        item = item->next;
    }
    if (!item)
        return FALSE;
    *output = item->qmsg;
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
