/* Public domain. No rights reserved. Wholly AI-authored. */
/* Frame restore geometry uses the public QWS_*RESTORE words. */
#include "os2libs/pmwin/internal.h"

enum
{
    FRAME_XRESTORE = 0x0c,
    FRAME_YRESTORE = 0x0e,
    FRAME_CXRESTORE = 0x10,
    FRAME_CYRESTORE = 0x12
};
BOOL prepareFrameState(Window *window, SWP *position)
{
    ULONG state = position->fl & (SWP_MINIMIZE | SWP_MAXIMIZE | SWP_RESTORE);
    if (!state)
        return TRUE;
    if ((state & (state - 1)) || state == SWP_MINIMIZE || strcmp(window->window_class_name, "#1") ||
        window->data_len < 0x14)
        return FALSE;
    if (sendWindowPos(window, WM_MINMAXFRAME, position))
    {
        position->fl &= ~(SWP_SIZE | SWP_MOVE);
        position->x = window->x;
        position->y = window->y;
        position->cx = window->w;
        position->cy = window->h;
        return TRUE;
    }
    if (state == SWP_MAXIMIZE)
    {
        if (!(window->style & (WS_MAXIMIZED | WS_MINIMIZED)))
        {
            WinSetWindowUShort(window->hwnd, FRAME_XRESTORE, window->w ? window->x : position->x);
            WinSetWindowUShort(window->hwnd, FRAME_YRESTORE, window->h ? window->y : position->y);
            WinSetWindowUShort(window->hwnd, FRAME_CXRESTORE, window->w ? window->w : position->cx);
            WinSetWindowUShort(window->hwnd, FRAME_CYRESTORE, window->h ? window->h : position->cy);
        }
        position->x = position->y = 0;
        position->cx = window->parent->w;
        position->cy = window->parent->h;
        position->fl |= SWP_MOVE | SWP_SIZE;
        window->style = (window->style & ~WS_MINIMIZED) | WS_MAXIMIZED;
    }
    else if (window->style & (WS_MAXIMIZED | WS_MINIMIZED))
    {
        if (!(position->fl & SWP_MOVE))
        {
            position->x = (SHORT)WinQueryWindowUShort(window->hwnd, FRAME_XRESTORE);
            position->y = (SHORT)WinQueryWindowUShort(window->hwnd, FRAME_YRESTORE);
        }
        if (!(position->fl & SWP_SIZE))
        {
            position->cx = WinQueryWindowUShort(window->hwnd, FRAME_CXRESTORE);
            position->cy = WinQueryWindowUShort(window->hwnd, FRAME_CYRESTORE);
        }
        position->fl |= SWP_MOVE | SWP_SIZE;
        window->style &= ~(WS_MAXIMIZED | WS_MINIMIZED);
    }
    return position->cx > 0 && position->cy > 0;
}
