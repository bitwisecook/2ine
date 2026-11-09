/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Window position, size, z-order, visibility and parent (WinSetWindowPos and
   its relatives), minimised and maximised positions, and flashing. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmgre/graphics.h"
#include "os2/ps/pmmerge/pmwin/paint.h"
#include "os2/include/private/pmp.h"
#include "emulator/abi.h"
#include <ctype.h>
#include "emulator/launch_record.h"
#include "os2/ps/pmmerge/controls/pmwctls/controls.h"
#include "os2/ps/pmmerge/pmwin/menus.h"

BOOL WinFlashWindow(HWND handle, BOOL flash)
{
    return (BOOL)(uintptr_t)WinSendMsg(handle, WM_FLASHWINDOW, (MPARAM)(uintptr_t)(flash != 0), 0);
}

BOOL WinGetMaxPosition(HWND handle, PSWP out)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = a ? getWindowFromHWND(a, handle) : NULL;
    if (!w || !out || !w->parent)
        return FALSE;
    /* A maximized frame's border hangs outside its parent. */
    POINTL border = isFrameWindow(w) ? frameBorderSize(w) : (POINTL){0, 0};
    out->hwnd = handle;
    out->x = -border.x;
    out->y = -border.y;
    out->cx = w->parent->w + border.x * 2;
    out->cy = w->parent->h + border.y * 2;
    out->fl |= SWP_SIZE | SWP_MOVE;
    return TRUE;
}

static BOOL slot_taken(const Window *parent, const Window *self, const RECTL *slot, LONG cx,
                       LONG cy)
{
    for (Window *s = parent->children; s; s = s->sibling)
    {
        if (s == self || !(s->style & WS_MINIMIZED) || s->data_len < 0x18)
            continue;
        LONG x = (SHORT)WinQueryWindowUShort(s->hwnd, 0x14),
             y = (SHORT)WinQueryWindowUShort(s->hwnd, 0x16);
        if (x < slot->xRight && slot->xLeft < x + cx && y < slot->yTop && slot->yBottom < y + cy)
            return TRUE;
    }
    return FALSE;
}

BOOL WinGetMinPosition(HWND handle, PSWP out, PPOINTL preferred)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = a ? getWindowFromHWND(a, handle) : NULL;
    if (!w || !out || !w->parent)
        return FALSE;
    POINTL border = isFrameWindow(w) ? frameBorderSize(w) : (POINTL){0, 0};
    LONG width = w->parent->w, height = w->parent->h;
    LONG icon_x = WinQuerySysValue(HWND_DESKTOP, 38), icon_y = WinQuerySysValue(HWND_DESKTOP, 39);
    LONG text_x = WinQuerySysValue(HWND_DESKTOP, 68), text_y = WinQuerySysValue(HWND_DESKTOP, 30);
    LONG pad_x = WinQuerySysValue(HWND_DESKTOP, 26), pad_y = WinQuerySysValue(HWND_DESKTOP, 27);
    /* Icon cells leave room for the text beneath, and widen to fill each row evenly. */
    LONG cell_x = (text_x > icon_x + border.x * 2 ? text_x : icon_x + border.x * 2) + pad_x * 4;
    LONG cell_y = icon_y + border.y * 2 + text_y;
    if (cell_x <= 0 || cell_y <= 0)
        return FALSE;
    if (cell_x <= width && width / (width / cell_x) > cell_x)
        cell_x = width / (width / cell_x);
    LONG columns = width / cell_x > 0 ? width / cell_x : 1;
    LONG rows = height / cell_y > 0 ? height / cell_y : 1;
    LONG slot_width = cell_x + (width - text_x * columns) / columns;
    LONG want_column = 0, want_row = 0;
    if (preferred)
    {
        want_column =
            clamp_long(width ? (LONG)((int64_t)preferred->x * columns / width) : 0, 0, columns - 1);
        want_row =
            clamp_long(height ? (LONG)((int64_t)preferred->y * rows / height) : 0, 0, rows - 1);
    }
    LONG best_column = want_column, best_row = want_row, bottom = 0;
    int64_t best = -1;
    for (LONG row = 0; row < rows; row++)
        for (LONG column = 0; column < columns; column++)
        {
            RECTL slot = {(LONG)(((int64_t)column * width + columns / 2) / columns),
                          (LONG)(((int64_t)row * height + rows / 2) / rows),
                          (LONG)(((int64_t)(column + 1) * width + columns / 2) / columns),
                          (LONG)(((int64_t)(row + 1) * height + rows / 2) / rows)};
            if (slot_taken(w->parent, w, &slot, icon_x, icon_y))
                continue;
            int64_t dx = column - want_column, dy = row - want_row, distance = dx * dx + dy * dy;
            if (best < 0 || distance < best)
            {
                best = distance;
                best_column = column;
                best_row = row;
                bottom = slot.yBottom;
            }
        }
    LONG x = best_column * cell_x + (slot_width - icon_x) / 2, y = text_y + pad_y * 2 + bottom;
    w->style |= WS_MINIMIZED;
    if (w->data_len >= 0x18)
    {
        WinSetWindowUShort(handle, 0x14, (USHORT)x);
        WinSetWindowUShort(handle, 0x16, (USHORT)y);
    }
    out->hwnd = handle;
    out->x = x - border.x;
    out->y = y - border.y;
    out->cx = icon_x + border.x * 2;
    out->cy = icon_y + border.y * 2;
    out->fl |= SWP_SIZE | SWP_MOVE;
    return TRUE;
}

BOOL WinIsWindowShowing(HWND handle)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    Window *w = a ? getWindowFromHWND(a, handle) : NULL;
    if (!w)
        return FALSE;
    for (Window *p = w; p && p != &desktop_window; p = p->parent)
        if (!(p->style & WS_VISIBLE) || (p->style & WS_MINIMIZED))
            return FALSE;
    /* Showing means the window's visible region is not empty. */
    HPS space = acquirePresentationSpace(handle, 0, NULL, FALSE);
    PresentationSpace *ps = space ? getPresentationSpaceFromHPS(a, space) : NULL;
    if (!ps)
        return TRUE;
    PmRegion visible = {0};
    BOOL known = lx_pm_window_clip(space, NULL, &visible), showing = !known || visible.count;
    free(visible.rects);
    destroyPresentationSpace(a, ps);
    return showing;
}

BOOL WinSetMultWindowPos(HAB hab, PSWP positions, ULONG count)
{
    AnchorBlock *a = getAnchorBlock(hab);
    if (!a || count > 65536 || (count && !positions))
        return FALSE;
    for (ULONG i = 0; i < count; i++)
    {
        if (positions[i].fl > 0xffff)
        {
            WinSetErrorInfo(PMERR_INVALID_FLAG, 0);
            return FALSE;
        }
        if (!getWindowFromHWND(a, positions[i].hwnd))
            return FALSE;
    }
    for (ULONG i = 0; i < count; i++)
    {
        SWP *p = &positions[i];
        if (!WinSetWindowPos(p->hwnd, p->hwndInsertBehind, p->x, p->y, p->cx, p->cy, p->fl))
            return FALSE;
    }
    return TRUE;
}

static BOOL set_window_order(AnchorBlock *anchor, Window *window, HWND behind)
{
    if (!window->parent)
        return FALSE;
    if (behind == window->hwnd)
        return TRUE;
    Window *previous = NULL;
    if (behind != HWND_TOP && behind != HWND_BOTTOM)
    {
        previous = getWindowFromHWND(anchor, behind);
        if (!previous || previous->parent != window->parent || previous->destroying)
            return FALSE;
    }
    Window **slot = &window->parent->children;
    while (*slot && *slot != window)
        slot = &(*slot)->sibling;
    if (!*slot)
        return FALSE;
    *slot = window->sibling;
    if (behind == HWND_BOTTOM)
    {
        slot = &window->parent->children;
        while (*slot)
            slot = &(*slot)->sibling;
    }
    else
        slot = previous ? &previous->sibling : &window->parent->children;
    window->sibling = *slot;
    *slot = window;
    return TRUE;
}

/* A launcher's show state and placement apply to the first frame shown. */
static void applyLaunchPresentation(const Window *win, LONG *x, LONG *y, LONG *cx, LONG *cy,
                                    ULONG *flags)
{
    LxLaunchPresentation launch;
    if (!(*flags & SWP_SHOW) || win->parent != &desktop_window || !isFrameWindow(win) ||
        !lx_launch_take_presentation(&launch))
        return;
    if (launch.control & LX_LAUNCH_INVISIBLE)
    {
        *flags = (*flags & ~(ULONG)(SWP_SHOW | SWP_ACTIVATE)) | SWP_HIDE;
        return;
    }
    if (launch.control & LX_LAUNCH_SET_POSITION)
    {
        *x = launch.x;
        *y = launch.y;
        *flags |= SWP_MOVE;
    }
    if ((launch.control & LX_LAUNCH_SET_SIZE) && launch.cx > 0 && launch.cy > 0)
    {
        *cx = launch.cx;
        *cy = launch.cy;
        *flags |= SWP_SIZE;
    }
    if (launch.control & LX_LAUNCH_MINIMIZE)
        *flags |= SWP_MINIMIZE;
    else if (launch.control & LX_LAUNCH_MAXIMIZE)
        *flags |= SWP_MAXIMIZE;
    if (launch.control & LX_LAUNCH_BACKGROUND)
        *flags &= ~(ULONG)SWP_ACTIVATE;
}

BOOL WinSetWindowPos(HWND hwnd, HWND behind, LONG x, LONG y, LONG cx, LONG cy, ULONG flags)
{
    TRACE_NATIVE("WinSetWindowPos(%u, %u, %d, %d, %d, %d, %u)", (uint)hwnd, (uint)behind, (int)x,
                 (int)y, (int)cx, (int)cy, (uint)flags);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *win = anchor ? getWindowFromHWND(anchor, hwnd) : NULL;
    if (win)
        applyLaunchPresentation(win, &x, &y, &cx, &cy, &flags);
    if (!win || ((flags & SWP_SIZE) && (cx <= 0 || cy <= 0)))
        return FALSE;
    SWP swp = {flags,
               (flags & SWP_SIZE) ? cy : win->h,
               (flags & SWP_SIZE) ? cx : win->w,
               (flags & SWP_MOVE) ? y : win->y,
               (flags & SWP_MOVE) ? x : win->x,
               behind,
               hwnd,
               0,
               0};
    if (!prepareFrameState(win, &swp))
        return FALSE;
    flags = swp.fl;
    if (frameControlSuppressed(win))
    {
        if (!win->frame_chrome_hidden)
            win->frame_chrome_visible = (win->style & WS_VISIBLE) != 0;
        if ((flags & SWP_SHOW) && !win->parent->formatting_frame)
            win->frame_chrome_visible = TRUE;
        if ((flags & SWP_HIDE) && !win->parent->formatting_frame)
            win->frame_chrome_visible = FALSE;
        win->frame_chrome_hidden = TRUE;
        flags = (flags & ~SWP_SHOW) | SWP_HIDE;
    }
    else if (win->frame_chrome_hidden)
    {
        if (!(flags & (SWP_SHOW | SWP_HIDE)))
            flags |= win->frame_chrome_visible ? SWP_SHOW : SWP_HIDE;
        win->frame_chrome_hidden = FALSE;
    }
    swp.fl = flags;
    if (!(flags & SWP_NOADJUST))
        sendWindowPos(win, WM_ADJUSTWINDOWPOS, &swp);
    win = getWindowFromHWND(anchor, hwnd);
    if (!win || win->destroying)
        return FALSE;
    flags = swp.fl;
    if ((flags & SWP_ZORDER) && !set_window_order(anchor, win, swp.hwndInsertBehind))
        return FALSE;
    LONG oldw = win->w, oldh = win->h;
    RECTL exposed = {win->x, win->y, win->x + oldw, win->y + oldh};
    win->x = swp.x;
    win->y = swp.y;
    win->w = swp.cx;
    win->h = swp.cy;
    BOOL was_visible = (win->style & WS_VISIBLE) != 0;
    if (flags & SWP_SHOW)
        win->style |= WS_VISIBLE;
    if (flags & SWP_HIDE)
        win->style &= ~WS_VISIBLE;
    if (win->parent == &desktop_window)
    {
        updateWindowCyclePolicy(win);
        /* The host refuses positions above the usable display area, for instance a placement
           saved on a taller screen. PM must record where the window actually lands. */
        int host_y = desktop_window.h - win->y - win->h;
        lx_host_window_constrain(win->heavy.window, win->x, &host_y, win->w, win->h);
        win->y = desktop_window.h - host_y - win->h;
        if (!lx_host_window_position(win->heavy.window, win->x, host_y, win->w, win->h))
            return FALSE;
        if ((win->style & WS_VISIBLE) && !(win->style & WS_MINIMIZED))
            lx_host_window_show(win->heavy.window);
        else if (!(win->style & WS_VISIBLE))
            lx_host_window_hide(win->heavy.window);
    }
    if (was_visible != ((win->style & WS_VISIBLE) != 0))
        sendMessage(win, WM_SHOW, (MPARAM)(uintptr_t)((win->style & WS_VISIBLE) != 0), 0);
    win = getWindowFromHWND(anchor, hwnd);
    if (!win || win->destroying)
        return FALSE;
    if (was_visible && win->parent != &desktop_window && !(flags & SWP_NOREDRAW) &&
        ((flags & SWP_HIDE) || win->x != exposed.xLeft || win->y != exposed.yBottom ||
         win->w != oldw || win->h != oldh))
        WinInvalidateRect(win->parent->hwnd, &exposed, TRUE);
    if (oldw != win->w || oldh != win->h)
    {
        sendMessage(win, WM_SIZE, (MPARAM)(uintptr_t)(((uint32)(uint16)oldh << 16) | (uint16)oldw),
                    (MPARAM)(uintptr_t)(((uint32)(uint16)win->h << 16) | (uint16)win->w));
        win = getWindowFromHWND(anchor, hwnd);
        if (!win || win->destroying)
            return FALSE;
        if (isFrameWindow(win))
            formatFrame(win);
        HWND menu = isFrameWindow(win) ? 0 : WinWindowFromID(win->hwnd, FID_MENU);
        LONG menu_height = menu ? lx_pm_display_scale(24) : 0;
        if (menu && win->h > menu_height)
            WinSetWindowPos(menu, HWND_TOP, 0, win->h - menu_height, win->w, menu_height,
                            SWP_MOVE | SWP_SIZE | SWP_SHOW);
        win = getWindowFromHWND(anchor, hwnd);
        if (!win || win->destroying)
            return FALSE;
        for (Window *child = win->children; child; child = child->sibling)
            if (!isFrameWindow(win) && child->id == FID_CLIENT && win->h > menu_height)
                WinSetWindowPos(child->hwnd, HWND_BOTTOM, 0, 0, win->w, win->h - menu_height,
                                SWP_MOVE | SWP_SIZE | SWP_SHOW);
    }
    else if (isFrameWindow(win) && (flags & (SWP_MINIMIZE | SWP_MAXIMIZE | SWP_RESTORE)))
        formatFrame(win);
    win = getWindowFromHWND(anchor, hwnd);
    if (!win || win->destroying)
        return FALSE;
    if (oldw != win->w || oldh != win->h)
        for (Window *child = win->children; child; child = child->sibling)
            if (child->style & WS_MAXIMIZED)
                WinSetWindowPos(child->hwnd, HWND_TOP, 0, 0, win->w, win->h,
                                SWP_MOVE | SWP_SIZE | SWP_NOADJUST);
    win = getWindowFromHWND(anchor, hwnd);
    if (!win || win->destroying)
        return FALSE;
    if (flags & (SWP_SIZE | SWP_MOVE | SWP_SHOW | SWP_HIDE))
        notifyVisibleRegions(anchor, win);
    if (!(flags & SWP_NOREDRAW) && (flags & (SWP_SIZE | SWP_SHOW | SWP_MOVE)))
        postExposedMessages(anchor, win, currentSystemTicks());
    if ((flags & SWP_ZORDER) && !(flags & SWP_NOREDRAW))
        WinInvalidateRect(win->parent->hwnd, NULL, TRUE);
    if ((flags & (SWP_ZORDER | SWP_ACTIVATE)) == (SWP_ZORDER | SWP_ACTIVATE) &&
        swp.hwndInsertBehind == HWND_TOP && win->parent == &desktop_window)
        lx_host_window_focus(win->heavy.window);
    if (flags & SWP_ACTIVATE)
        WinSetActiveWindow(HWND_DESKTOP, topLevelWindow(anchor, hwnd));
    return TRUE;
}

BOOL WinQueryWindowPos(HWND hwnd, PSWP position)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *win = anchor ? getWindowFromHWND(anchor, hwnd) : NULL;
    if (!win || !position)
        return FALSE;
    *position = (SWP){((win->style & WS_VISIBLE) ? SWP_SHOW : SWP_HIDE) |
                          ((win->style & WS_MAXIMIZED) ? SWP_MAXIMIZE : 0) |
                          ((win->style & WS_MINIMIZED) ? SWP_MINIMIZE : 0),
                      win->h,
                      win->w,
                      win->y,
                      win->x,
                      HWND_TOP,
                      hwnd,
                      0,
                      0};
    return TRUE;
}

BOOL WinQueryWindowRect(HWND hwnd, PRECTL rect)
{
    SWP position;
    if (!rect || !WinQueryWindowPos(hwnd, &position))
        return FALSE;
    *rect = (RECTL){0, 0, position.cx, position.cy};
    return TRUE;
}

BOOL WinShowWindow(HWND window, BOOL show)
{
    return WinSetWindowPos(window, HWND_TOP, 0, 0, 0, 0, show ? SWP_SHOW : SWP_HIDE);
}

static void setDrawable(Window *window, LxHostWindow *drawable)
{
    window->heavy.window = drawable;
    for (Window *child = window->children; child; child = child->sibling)
        setDrawable(child, drawable);
}

BOOL WinSetParent(HWND handle, HWND parent_handle, BOOL redraw)
{
    TRACE_NATIVE("WinSetParent(%u, %u, %u)", (uint)handle, (uint)parent_handle, (uint)redraw);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    Window *parent = anchor ? getWindowFromHWND(anchor, parent_handle) : NULL;
    if (!window || !parent || !window->parent || window->destroying || parent->destroying)
        return FALSE;
    for (Window *ancestor = parent; ancestor; ancestor = ancestor->parent)
        if (ancestor == window)
            return FALSE;
    Window *old_parent = window->parent;
    if (old_parent == parent)
        return TRUE;
    LxHostWindow *drawable = parent->heavy.window;
    if (parent == &desktop_window)
    {
        drawable = lx_host_window_create(
            window->text, window->x, desktop_window.h - window->y - window->h,
            window->w > 0 ? window->w : 1, window->h > 0 ? window->h : 1, window);
        if (!drawable)
            return FALSE;
    }
    Window **link = &old_parent->children;
    while (*link && *link != window)
        link = &(*link)->sibling;
    if (!*link)
    {
        if (parent == &desktop_window)
            lx_host_window_destroy(drawable);
        return FALSE;
    }
    *link = window->sibling;
    window->sibling = parent->children;
    parent->children = window;
    LxHostWindow *old_drawable = old_parent == &desktop_window ? window->heavy.window : NULL;
    window->parent = parent;
    setDrawable(window, drawable);
    updateWindowCyclePolicy(old_parent);
    applyHostWindowChrome(window);
    updateWindowCyclePolicy(window);
    if (isFrameWindow(window))
        formatFrame(window);
    if (old_drawable)
        lx_host_window_destroy(old_drawable);
    if (parent == &desktop_window && (window->style & WS_VISIBLE))
        lx_host_window_show(drawable);
    notifyVisibleRegions(anchor, window);
    if (redraw && (window->style & WS_VISIBLE))
    {
        if (old_parent != &desktop_window && old_parent != &anchor->object_window)
            WinInvalidateRect(old_parent->hwnd, NULL, TRUE);
        WinInvalidateRect(handle, NULL, TRUE);
    }
    return TRUE;
}
