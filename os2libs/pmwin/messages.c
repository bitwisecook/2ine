/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/video_frames.h"
#include "os2libs/pmwin/input.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmwin/menus.h"
#include "os2libs/pmwin/open32_post.h"
#include "guest/launch_record.h"

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

typedef struct PaintScope
{
    AnchorBlock *anchor;
    HWND window;
    struct PaintScope *previous;
} PaintScope;

static LX_HOST_THREAD_LOCAL PaintScope *ActivePaint;

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
    if (GLoaderState.execution && (uintptr_t)winproc <= UINT32_MAX &&
        lx_execution_memory(GLoaderState.execution, (uint32_t)(uintptr_t)winproc, 1,
                            LX_GUEST_EXECUTE))
        result = (MRESULT)(uintptr_t)lx_execution_callback32_4(
            GLoaderState.execution, (uint32_t)(uintptr_t)winproc, win->hwnd, msg,
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

/* SV_BEGINDRAG through SV_ENDSELECT name the raw event behind each semantic message. */
static BOOL post_semantic(AnchorBlock *anchor, Window *win, ULONG raw, MPARAM point, ULONG ticks)
{
    BOOL posted = FALSE;
    ULONG shift = anchor->modifiers & 0x38; /* KC_SHIFT, KC_CTRL, KC_ALT */
    for (LONG i = 0; i < 9; i++)
    {
        ULONG value = (ULONG)WinQuerySysValue(HWND_DESKTOP, 75 + i);
        if ((value & 0xffff) == raw && value >> 16 == shift)
            posted |= postTimestampedMsg(anchor, win->hwnd, WM_BEGINDRAG + i, point,
                                         (MPARAM)(uintptr_t)TRUE, ticks);
    }
    return posted;
}

BOOL postSemanticKey(AnchorBlock *anchor, HWND window, ULONG key, ULONG shift, ULONG ticks)
{
    BOOL posted = FALSE;
    for (LONG i = 0; i < 9; i++)
    {
        ULONG value = (ULONG)WinQuerySysValue(HWND_DESKTOP, 84 + i);
        if (value && (value & 0xffff) == key && value >> 16 == (shift & 0x38))
            posted |= postTimestampedMsg(anchor, window, WM_BEGINDRAG + i, 0, 0, ticks);
    }
    return posted;
}

/* Motion thresholds and chords derive further messages from the raw pointer stream. */
static BOOL post_derived(AnchorBlock *anchor, Window *win, const LxHostEvent *event, ULONG button,
                         LONG x, LONG y, MPARAM point, ULONG ticks)
{
    BOOL posted = FALSE;
    if (event->type == LX_HOST_EVENT_MOTION)
    {
        for (unsigned b = 0; b < 3; b++)
            if (anchor->press_down[b] && !anchor->press_moving[b] &&
                (labs(x - anchor->press_x[b]) > WinQuerySysValue(HWND_DESKTOP, 73) ||
                 labs(y - anchor->press_y[b]) > WinQuerySysValue(HWND_DESKTOP, 74)))
            {
                ULONG start = WM_BUTTON1MOTIONSTART + b * 3;
                anchor->press_moving[b] = TRUE;
                posted |= postTimestampedMsg(anchor, win->hwnd, start, point, 0, ticks);
                posted |= post_semantic(anchor, win, start, point, ticks);
            }
        return posted;
    }
    if (!button || button > 3)
        return FALSE;
    unsigned b = button - 1;
    if (event->type == LX_HOST_EVENT_BUTTON_DOWN)
    {
        anchor->press_down[b] = TRUE;
        anchor->press_moving[b] = FALSE;
        anchor->press_ticks[b] = ticks;
        anchor->press_x[b] = x;
        anchor->press_y[b] = y;
        unsigned other = b ^ 1; /* A chord pairs the first two buttons. */
        if (b < 2 && anchor->press_down[other] &&
            ticks - anchor->press_ticks[other] <= (ULONG)WinQuerySysValue(HWND_DESKTOP, 70) &&
            labs(x - anchor->press_x[other]) <= WinQuerySysValue(HWND_DESKTOP, 71) &&
            labs(y - anchor->press_y[other]) <= WinQuerySysValue(HWND_DESKTOP, 72))
            posted |= postTimestampedMsg(anchor, win->hwnd, WM_CHORD, point, 0, ticks);
    }
    else if (event->type == LX_HOST_EVENT_BUTTON_UP)
    {
        if (anchor->press_moving[b])
        {
            ULONG end = WM_BUTTON1MOTIONEND + b * 3;
            posted |= postTimestampedMsg(anchor, win->hwnd, end, point, 0, ticks);
            posted |= post_semantic(anchor, win, end, point, ticks);
        }
        anchor->press_down[b] = anchor->press_moving[b] = FALSE;
    }
    return posted;
}

BOOL processHostEvent(AnchorBlock *anchor, const LxHostEvent *event)
{
    ULONG ticks = (ULONG)event->timestamp;
    Window *win = event->window_data;
    if (event->type == LX_HOST_EVENT_QUIT)
        return anchor->message_queue.cancel_shutdown ||
               postTimestampedMsg(anchor, 0, WM_QUIT, 0, 0, ticks);
    if (!win || !win->hwnd)
        return FALSE;
    if (isFrameWindow(win) &&
        (event->type == LX_HOST_EVENT_MINIMIZED || event->type == LX_HOST_EVENT_RESTORED))
    {
        BOOL minimized = (win->style & WS_MINIMIZED) != 0;
        if (minimized != (event->type == LX_HOST_EVENT_MINIMIZED))
            return WinSetWindowPos(win->hwnd, HWND_TOP, 0, 0, 0, 0,
                                   minimized ? SWP_RESTORE : SWP_MINIMIZE);
        return FALSE;
    }
    if ((event->type == LX_HOST_EVENT_BUTTON_DOWN || event->type == LX_HOST_EVENT_BUTTON_UP) &&
        event->button >= 1 && event->button <= 3)
    {
        uint8 *state = &anchor->physical_buttons[event->button - 1];
        *state = event->type == LX_HOST_EVENT_BUTTON_DOWN ? 3 : (*state & 1);
    }
    if (event->type == LX_HOST_EVENT_INPUT_RESET)
    {
        memset(anchor->physical_buttons, 0, sizeof anchor->physical_buttons);
        return releaseInputState(anchor, ticks);
    }
    if (event->type == LX_HOST_EVENT_KEY_DOWN || event->type == LX_HOST_EVENT_KEY_UP ||
        event->type == LX_HOST_EVENT_TEXT)
        return processKeyboardEvent(anchor, event);
    if (event->type == LX_HOST_EVENT_FOCUS_GAINED)
    {
        /* The host already focused this window. Requesting that again would
           answer each focus event with another, which never settles while two
           windows have raises in flight. */
        lxPmHostFocusReport = TRUE;
        WinSetActiveWindow(HWND_DESKTOP, win->hwnd);
        if (!WinQueryFocus(HWND_DESKTOP))
            WinSetFocus(HWND_DESKTOP, win->last_focus ? win->last_focus : win->hwnd);
        lxPmHostFocusReport = FALSE;
        return FALSE;
    }
    if (event->type == LX_HOST_EVENT_FOCUS_LOST)
    {
        if (anchor->active == win->hwnd)
            WinSetActiveWindow(HWND_DESKTOP, 0);
        else if (topLevelWindow(anchor, anchor->focus) == win->hwnd)
            WinSetFocus(HWND_DESKTOP, 0);
        memset(anchor->key_down, 0, sizeof anchor->key_down);
        memset(anchor->physical_keys, 0, sizeof anchor->physical_keys);
        memset(anchor->physical_buttons, 0, sizeof anchor->physical_buttons);
        anchor->modifiers = anchor->text_modifiers = anchor->text_scancode = anchor->text_repeat =
            0;
        return FALSE;
    }
    if (event->type == LX_HOST_EVENT_CLOSE)
        return postTimestampedMsg(anchor, win->hwnd, WM_CLOSE, 0, 0, ticks);
    if (event->type == LX_HOST_EVENT_SHOWN || event->type == LX_HOST_EVENT_EXPOSED)
        return postExposedMessages(anchor, win, ticks);
    if (event->type == LX_HOST_EVENT_MOVED)
    {
        LONG y = desktop_window.h - event->y - win->h;
        if (event->x == win->x && y == win->y)
            return FALSE;
        /* The host already moved it. Reissuing the move would feed stale
           queued position events back into the native window manager. */
        win->x = event->x;
        win->y = y;
        notifyVisibleRegions(anchor, win);
        SWP position = {SWP_MOVE, win->h, win->w, y, win->x, HWND_TOP, win->hwnd, 0, 0};
        sendWindowPos(win, WM_WINDOWPOSCHANGED, &position);
        return TRUE;
    }
    if (event->type == LX_HOST_EVENT_RESIZED)
    {
        if (event->width == win->w && event->height == win->h)
            return FALSE;
        if (isFrameWindow(win) && !frameResizable(win))
        {
            lx_host_window_position(win->heavy.window, win->x, desktop_window.h - win->y - win->h,
                                    win->w, win->h);
            return FALSE;
        }
        return WinSetWindowPos(win->hwnd, HWND_TOP, win->x, win->y, event->width, event->height,
                               SWP_SIZE | SWP_NOADJUST);
    }
    if (event->type != LX_HOST_EVENT_MOTION && event->type != LX_HOST_EVENT_BUTTON_DOWN &&
        event->type != LX_HOST_EVENT_BUTTON_UP)
        return FALSE;
    int x = event->x, y = win->h - 1 - event->y;
    LONG origin_x = 0, origin_y = 0;
    for (Window *w = win; w && w != &desktop_window; w = w->parent)
    {
        origin_x += w->x;
        origin_y += w->y;
    }
    LONG desktop_x = origin_x + x, desktop_y = origin_y + y;
    if (lx_pm_clip_pointer(&desktop_x, &desktop_y))
    {
        x = desktop_x - origin_x;
        y = desktop_y - origin_y;
        if (event->type == LX_HOST_EVENT_MOTION)
            lx_host_mouse_warp(desktop_x, desktop_window.h - 1 - desktop_y);
    }
    Window *capture = getWindowFromHWND(anchor, anchor->capture);
    if (capture)
    {
        POINTL point = {x, y};
        if (!WinMapWindowPoints(win->hwnd, capture->hwnd, &point, 1))
            return FALSE;
        x = point.x;
        y = point.y;
        win = capture;
    }
    for (; !capture;)
    {
        Window *child;
        for (child = win->children; child; child = child->sibling)
            if ((child->style & WS_VISIBLE) && x >= child->x && y >= child->y &&
                x < child->x + child->w && y < child->y + child->h)
            {
                MPARAM local = (MPARAM)(uintptr_t)((uint16)(x - child->x) |
                                                   (uint32)(uint16)(y - child->y) << 16);
                if ((child->class_style & CS_HITTEST) &&
                    (LONG)(uintptr_t)sendMessage(child, WM_HITTEST, local, 0) == HT_TRANSPARENT)
                    continue;
                break;
            }
        if (!child)
            break;
        x -= child->x;
        y -= child->y;
        win = child;
    }
    if (!windowAcceptsInput(win))
        return FALSE;
    if (event->type == LX_HOST_EVENT_BUTTON_DOWN)
        dismissMenusForClick(anchor, win);
    ULONG message = WM_MOUSEMOVE, button = 0;
    if (event->type != LX_HOST_EVENT_MOTION)
    {
        if (!event->button)
            return FALSE;
        /* SV_SWAPBUTTON exchanges the primary and secondary buttons. */
        button = event->button;
        if (button <= 2 && WinQuerySysValue(HWND_DESKTOP, 0))
            button = 3 - button;
        message = WM_BUTTON1DOWN + (button - 1) * 3 + (event->type == LX_HOST_EVENT_BUTTON_UP);
        if (event->type == LX_HOST_EVENT_BUTTON_DOWN)
        {
            /* SV_DBLCLKTIME and the SV_CXDBLCLK rectangle define a double click. */
            LONG dx = x - anchor->click_x, dy = y - anchor->click_y;
            BOOL second = anchor->click_window == win->hwnd && anchor->click_button == button &&
                          ticks - anchor->click_ticks <= (ULONG)WinQuerySysValue(HWND_DESKTOP, 1) &&
                          labs(dx) <= WinQuerySysValue(HWND_DESKTOP, 2) &&
                          labs(dy) <= WinQuerySysValue(HWND_DESKTOP, 3);
            if (second)
                message = WM_BUTTON1DBLCLK + (button - 1) * 3;
            anchor->click_window = second ? 0 : win->hwnd;
            anchor->click_button = button;
            anchor->click_ticks = ticks;
            anchor->click_x = x;
            anchor->click_y = y;
        }
    }
    MPARAM point = (MPARAM)(uintptr_t)(((uint32)(uint16)y << 16) | (uint16)x);
    BOOL result = postTimestampedMsg(anchor, win->hwnd, message, point, 0, ticks);
    if (event->type != LX_HOST_EVENT_MOTION)
        result |= post_semantic(anchor, win, message, point, ticks);
    if (event->type == LX_HOST_EVENT_BUTTON_UP)
    {
        ULONG click = WM_BUTTON1CLICK + (button - 1) * 3;
        result |= postTimestampedMsg(anchor, win->hwnd, click, point, 0, ticks);
        result |= post_semantic(anchor, win, click, point, ticks);
    }
    result |= post_derived(anchor, win, event, button, x, y, point, ticks);
    return result;
}

void presentWindow(AnchorBlock *anchor, Window *window)
{
    if (!window || !window->heavy.window)
        return;
    /* A synchronous modal loop can run inside another window's WM_PAINT.
       Defer only canvases involved in an active paint callback, allowing a
       separate modal host window to present while that callback is waiting. */
    for (PaintScope *scope = ActivePaint; scope; scope = scope->previous)
    {
        Window *painting =
            scope->anchor == anchor ? getWindowFromHWND(anchor, scope->window) : NULL;
        if (painting && painting->heavy.window == window->heavy.window)
            return;
    }
    Window *root = window;
    while (root->parent && root->parent != &desktop_window)
        root = root->parent;
    uint64_t now = lx_host_video_ticks_ms();
    for (size_t i = 0; i < anchor->windows_array_len; i++)
    {
        Window *pending = anchor->windows[i];
        if (pending && pending->hwnd && pending->heavy.window == window->heavy.window &&
            pending->w > 0 && pending->h > 0 && pending->paint_pending &&
            !WinIsRectEmpty(0, &pending->invalid) && windowAcceptsUpdates(pending) &&
            (!pending->paint_offered || now - root->last_present < 16))
            return;
    }
    lx_host_window_present(window->heavy.window);
    root->last_present = now;
}

static void presentWindows(AnchorBlock *anchor)
{
    for (size_t i = 0; i < anchor->windows_array_len; i++)
    {
        Window *win = anchor->windows[i];
        if (win && win->hwnd && win->parent == &desktop_window && (win->style & WS_VISIBLE))
            presentWindow(anchor, win);
    }
}

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
        lx_execution_poll_termination(GLoaderState.execution);
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

/* A window procedure may be guest code, so the rectangle travels in guest memory. */
BOOL sendEraseBackground(Window *client, HPS space, const RECTL *rectangle)
{
    if (!TWOINE_PORTABLE_LOADER || !GLoaderState.execution)
        return sendMessage(client, WM_ERASEBACKGROUND, (MPARAM)(uintptr_t)space,
                           (MPARAM)rectangle) != 0;
    LxGuestMapping *mapping = lx_execution_map(GLoaderState.execution, 0, 4096, 3, 0);
    if (!mapping)
        return FALSE;
    memcpy(lx_execution_mapping_host(mapping), rectangle, sizeof *rectangle);
    BOOL result = sendMessage(client, WM_ERASEBACKGROUND, (MPARAM)(uintptr_t)space,
                              (MPARAM)(uintptr_t)lx_execution_mapping_address(mapping)) != 0;
    lx_execution_unmap(GLoaderState.execution, mapping);
    return result;
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

BOOL lx_pm_poll_input(void)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return FALSE;
    LxHostEvent event;
    while (lx_host_video_poll(&event))
        lx_pm_host_event(&event);
    return TRUE;
}

LONG lx_pm_mouse_state(unsigned button)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || button < 1 || button > 3)
        return 0;
    unsigned state = anchor->physical_buttons[button - 1];
    anchor->physical_buttons[button - 1] &= ~1u;
    return (SHORT)((state & 2 ? 0x8000 : 0) | (state & 1));
}
