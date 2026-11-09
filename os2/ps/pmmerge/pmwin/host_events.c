/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Host input and window events become PM queue messages: pointer, keyboard,
   sizing, activation and close. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmp.h"
#include "os2/include/private/pmgrep.h"
#include "emulator/launch_record.h"
#include "emulator/abi.h"
#include <ctype.h>

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
