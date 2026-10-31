/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/pmwin/internal.h"
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
            return FALSE;
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

    return TRUE;
}

BOOL postTimestampedMsg(AnchorBlock *anchor, HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2,
                        ULONG ticks)
{
    const QMSG qmsg = {hwnd, msg, mp1, mp2, currentSystemTicks(), {0, 0}, 0};
    return postMessage(anchor, &qmsg);
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
            retval |= postTimestampedMsg(anchor, win->hwnd, WM_PAINT, 0, 0, ticks);
            retval |= postExposedMessages(anchor, win->children, ticks);
        }
    }
    return retval;
}

BOOL processHostEvent(AnchorBlock *anchor, const LxHostEvent *event)
{
    ULONG ticks = (ULONG)event->timestamp;
    switch (event->type)
    {
        case LX_HOST_EVENT_QUIT:
            return postTimestampedMsg(anchor, NULLHANDLE, WM_QUIT, 0, 0, ticks);
        case LX_HOST_EVENT_SHOWN:
        case LX_HOST_EVENT_EXPOSED:
            return event->window_data ? postExposedMessages(anchor, event->window_data, ticks)
                                      : FALSE;
        default:
            return FALSE;
    }
}

void pumpEvents(AnchorBlock *anchor)
{
    LxHostEvent event;
    BOOL queued = FALSE;

    while (lx_host_video_poll(&event))
    {
        queued |= processHostEvent(anchor, &event);
    }

    while (!queued)
    {
        while (!lx_host_video_wait(&event))
        {
            lx_host_sleep_ms(10);
        }
        queued |= processHostEvent(anchor, &event);
        while (lx_host_video_poll(&event))
        {
            queued |= processHostEvent(anchor, &event);
        }
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

            if (!anchor->message_queue.head)
            {
                assert(anchor->message_queue.tail == i);
                anchor->message_queue.tail = NULL;
            }

            i->next = anchor->message_queue.free_pool;
            anchor->message_queue.free_pool = i;

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
        return FALSE;
    }
    if (anchor->message_queue.hmq == NULLHANDLE)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_NO_MSG_QUEUE, FALSE);
    }

    Window *win = getWindowFromHWND(anchor, hwnd);
    if (!win)
    {
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
    /* Thread messages enter the queue without a target window procedure. */

    FIXME("need to be able to access other threads' message queues");
    return FALSE;
}
