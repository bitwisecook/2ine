/* Public domain. No rights reserved. Wholly AI-authored. */
/* Window placement: minimised and maximised positions, multiple positioning,
   showing state and flashing. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmwin/graphics.h"
#include "os2/ps/pmmerge/pmwin/paint.h"
#include "os2/include/private/pmp.h"
#include "emulator/abi.h"
#include <ctype.h>

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
