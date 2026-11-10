/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Message queues, sent and posted messages, broadcasts and message-name tracing. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmp.h"
#include "os2/include/private/pmgrep.h"
#include "emulator/launch_record.h"
#include "emulator/abi.h"
#include <ctype.h>

enum
{
    GUEST_PAGE_SIZE = 4096,
    /* MQINFO: cb, pid, tid, cmsgs and pReserved, as 32-bit guest fields. */
    MQINFO_PID_OFFSET = 4,
    MQINFO_TID_OFFSET = 8,
    MQINFO_CMSGS_OFFSET = 12,
    MQINFO_SIZE = 20,
    /* Undocumented WinBroadcastMsg flag: like BMSG_POSTQUEUE it addresses message
       queues, but it sends. */
    BMSG_SENDQUEUE = 0x0010,
    BMSG_VALID = BMSG_SEND | BMSG_POSTQUEUE | BMSG_DESCENDANTS | BMSG_FRAMEONLY | BMSG_SENDQUEUE
};

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

LX_HOST_THREAD_LOCAL PaintScope *ActivePaint;

MRESULT sendMessage(Window *win, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    PFNWP winproc = win->window_proc;
    if (!winproc)
    {
        winproc = WinDefWindowProc;
    }
    TRACE_EVENT("EVENT SEND { hwnd=%u, msg=%u (%s), mp1=%p, mp2=%p, proc=%p }", (uint)win->hwnd,
                (uint)msg, messageName(msg), mp1, mp2, winproc);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    HWND painted = msg == WM_PAINT ? win->hwnd : 0;
    PaintScope scope = {anchor, painted, ActivePaint};
    if (msg == WM_PAINT)
    {
        anchor->paint_depth++;
        ActivePaint = &scope;
    }
    MRESULT result;
    if (GProcessState.execution && (uintptr_t)winproc <= UINT32_MAX &&
        lx_execution_memory(GProcessState.execution, (uint32_t)(uintptr_t)winproc, 1,
                            LX_GUEST_EXECUTE))
        result = (MRESULT)(uintptr_t)lx_execution_callback32_4(
            GProcessState.execution, (uint32_t)(uintptr_t)winproc, win->hwnd, msg,
            (uint32_t)(uintptr_t)mp1, (uint32_t)(uintptr_t)mp2);
    else
        result = winproc(win->hwnd, msg, mp1, mp2);
    if (msg == WM_PAINT)
    {
        ActivePaint = scope.previous;
        anchor->paint_depth--;
        /* An animation may invalidate its next frame inside WM_PAINT. */
        if (getWindowFromHWND(anchor, painted) == win && win->paint_pending)
            win->paint_offered = TRUE;
    }
    return result;
}

BOOL destroyMessageQueue(AnchorBlock *anchor)
{
    retireAnchorMessages(anchor);
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
    registerMessageQueue(anchor);
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

static BOOL postExposedSiblings(AnchorBlock *anchor, Window *win, ULONG ticks)
{
    if (!win)
        return FALSE;
    BOOL result = postExposedSiblings(anchor, win->sibling, ticks);
    return postExposedMessages(anchor, win, ticks) | result;
}

BOOL postExposedMessages(AnchorBlock *anchor, Window *win, const ULONG ticks)
{
    BOOL retval = FALSE;
    if (win)
    {
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
            retval |= postExposedSiblings(anchor, win->children, ticks);
        }
    }
    return retval;
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

typedef struct QueuedSend
{
    HWND window;
    ULONG message;
    MPARAM first, second;
} QueuedSend;

static uintptr_t deliverQueuedSend(void *context)
{
    const QueuedSend *send = context;
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *win = anchor ? getWindowFromHWND(anchor, send->window) : NULL;
    return win ? (uintptr_t)sendMessage(win, send->message, send->first, send->second) : 0;
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
    QueuedSend send = {hwnd, msg, mp1, mp2};
    uintptr_t result = 0;
    lx_pm_window_call(hwnd, deliverQueuedSend, &send, sizeof send, &result);
    return (MRESULT)result;
}

BOOL WinPostMsg(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{

    TRACE_NATIVE("WinPostMsg(%u, %u, %p, %p)", (uint)hwnd, (uint)msg, mp1, mp2);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
    {
        return postForeignMessage(hwnd, msg, mp1, mp2);
    }
    Window *win = getWindowFromHWND(anchor, hwnd);
    if (hwnd && !win)
    {
        if (postForeignMessage(hwnd, msg, mp1, mp2))
            return TRUE;
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, FALSE);
    }
    if (anchor->message_queue.hmq == NULLHANDLE)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_NO_MSG_QUEUE, FALSE);
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
    if (!TWOINE_PORTABLE_LOADER || !GProcessState.execution)
        return sendMessage(win, msg, position, 0);
    LxGuestMapping *mapping = lx_execution_map(GProcessState.execution, 0, GUEST_PAGE_SIZE,
                                               LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
        return 0;
    memcpy(lx_execution_mapping_host(mapping), position, sizeof *position);
    MRESULT result =
        sendMessage(win, msg, (MPARAM)(uintptr_t)lx_execution_mapping_address(mapping), 0);
    memcpy(position, lx_execution_mapping_host(mapping), sizeof *position);
    lx_execution_unmap(GProcessState.execution, mapping);
    return result;
}

/* A window procedure may be guest code, so the rectangle travels in guest memory. */
BOOL sendEraseBackground(Window *client, HPS space, const RECTL *rectangle)
{
    if (!TWOINE_PORTABLE_LOADER || !GProcessState.execution)
        return sendMessage(client, WM_ERASEBACKGROUND, (MPARAM)(uintptr_t)space,
                           (MPARAM)rectangle) != 0;
    LxGuestMapping *mapping = lx_execution_map(GProcessState.execution, 0, GUEST_PAGE_SIZE,
                                               LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
        return FALSE;
    memcpy(lx_execution_mapping_host(mapping), rectangle, sizeof *rectangle);
    BOOL result = sendMessage(client, WM_ERASEBACKGROUND, (MPARAM)(uintptr_t)space,
                              (MPARAM)(uintptr_t)lx_execution_mapping_address(mapping)) != 0;
    lx_execution_unmap(GProcessState.execution, mapping);
    return result;
}

BOOL WinQueryMsgPos(HAB hab, PPOINTL out)
{
    AnchorBlock *a = getAnchorBlock(hab);
    if (!a || !out)
        return FALSE;
    *out = a->message_queue.last_position;
    return TRUE;
}

BOOL WinQueryQueueInfo(HMQ queue, PVOID out, ULONG bytes)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    if (!a || (a->message_queue.hmq != queue && queue != HMQ_CURRENT))
        return FALSE;
    /* Whole fields are copied, and a missing or short buffer is not an error. */
    bytes &= ~3u;
    if (!out || !bytes)
        return TRUE;
    uint8 data[MQINFO_SIZE] = {0};
    ULONG count = 0;
    for (MessageQueueItem *i = a->message_queue.head; i; i = i->next)
        count++;
    LxGuestTib *t = GProcessState.getOs2Tib ? GProcessState.getOs2Tib() : NULL;
    ULONG tid = 0;
    if (t)
    {
        uint8 b[4];
        if (lx_execution_read(GProcessState.execution, lx_guest_load_u32(&t->tib2), b, 4))
            tid = lx_guest_load_u32(b);
    }
    lx_guest_store_u32(data, MQINFO_SIZE);
    lx_guest_store_u32(data + MQINFO_PID_OFFSET, GProcessState.pib.process_id);
    lx_guest_store_u32(data + MQINFO_TID_OFFSET, tid);
    lx_guest_store_u32(data + MQINFO_CMSGS_OFFSET, count);
    memcpy(out, data, bytes < MQINFO_SIZE ? bytes : MQINFO_SIZE);
    return TRUE;
}

/* Sending can destroy windows, so targets are gathered before any delivery. */
static BOOL collect_targets(const Window *parent, ULONG flags, HWND **list, size_t *count,
                            size_t *capacity)
{
    for (Window *child = parent->children; child; child = child->sibling)
    {
        if (!(flags & BMSG_FRAMEONLY) || isFrameWindow(child))
        {
            if (*count == *capacity)
            {
                size_t grown = *capacity ? *capacity * 2 : 32;
                HWND *larger = realloc(*list, grown * sizeof *larger);
                if (!larger)
                    return FALSE;
                *list = larger;
                *capacity = grown;
            }
            (*list)[(*count)++] = child->hwnd;
        }
        if ((flags & BMSG_DESCENDANTS) && !collect_targets(child, flags, list, count, capacity))
            return FALSE;
    }
    return TRUE;
}

BOOL WinBroadcastMsg(HWND parent, ULONG message, MPARAM first, MPARAM second, ULONG flags)
{
    if (flags & ~(ULONG)BMSG_VALID)
    {
        WinSetErrorInfo(PMERR_INVALID_FLAG, 0);
        return FALSE;
    }
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = !a ? NULL : parent == HWND_DESKTOP ? &desktop_window : getWindowFromHWND(a, parent);
    if (!w)
        return FALSE;
    if (flags & (BMSG_POSTQUEUE | BMSG_SENDQUEUE))
        return broadcastQueues(a, message, first, second, !(flags & BMSG_POSTQUEUE));
    if (!w->children)
        return FALSE;
    HWND *targets = NULL;
    size_t count = 0, capacity = 0;
    BOOL ok = collect_targets(w, flags, &targets, &count, &capacity);
    for (size_t i = 0; i < count; i++)
    {
        if (flags & BMSG_SEND)
        {
            if (!WinSendMsg(targets[i], message, first, second))
                ok = FALSE;
        }
        else if (!WinPostMsg(targets[i], message, first, second))
            ok = FALSE;
    }
    free(targets);
    return ok;
}
