/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Presents finished host window canvases to the display. */
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

void presentWindows(AnchorBlock *anchor)
{
    for (size_t i = 0; i < anchor->windows_array_len; i++)
    {
        Window *win = anchor->windows[i];
        if (win && win->hwnd && win->parent == &desktop_window && (win->style & WS_VISIBLE))
            presentWindow(anchor, win);
    }
}
