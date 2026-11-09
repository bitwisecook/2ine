/* Public domain. No rights reserved. Wholly AI-authored. */
/* Mouse pointer state: the show count, the confining rectangle, the current cursor and the
   double-click time. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/private/pmp.h"

static _Thread_local LONG cursor_count;

LONG ShowCursor(BOOL show)
{
    LONG previous = cursor_count;
    if (show)
    {
        if (cursor_count < INT32_MAX)
            cursor_count++;
    }
    else if (cursor_count > INT32_MIN)
        cursor_count--;
    if ((previous < 0) != (cursor_count < 0))
        WinShowPointer(HWND_DESKTOP, cursor_count >= 0);
    return cursor_count;
}

void open32ClipCursorPoint(LONG *x, LONG *y)
{
    LONG height = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN), native = height - 1 - *y;
    lx_pm_clip_pointer(x, &native);
    *y = height - 1 - native;
}

BOOL ClipCursor(PVOID rectangle)
{
    if (!open32Presentation())
    {
        open32SetError(8);
        return FALSE;
    }
    if (!rectangle)
    {
        lx_pm_set_pointer_clip(NULL);
        return TRUE;
    }
    /* Windows bounds become inclusive PM desktop bounds limited to the screen. */
    const uint8 *wire = rectangle;
    LONG width = WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN),
         height = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
    LONG left = (LONG)lx_guest_load_u32(wire), top = height - (LONG)lx_guest_load_u32(wire + 4);
    LONG right = (LONG)lx_guest_load_u32(wire + 8);
    LONG bottom = height - (LONG)lx_guest_load_u32(wire + 12);
    RECTL bounds = {left > 0 ? left : 0, bottom > 0 ? bottom : 0,
                    right < width - 1 ? right : width - 1, top < height - 1 ? top : height - 1};
    lx_pm_set_pointer_clip(&bounds);
    POINTL point;
    if (WinQueryPointerPos(HWND_DESKTOP, &point))
        WinSetPointerPos(HWND_DESKTOP, point.x, point.y);
    return TRUE;
}

ULONG GetCursor(void)
{
    return open32Presentation() ? WinQueryPointer(HWND_DESKTOP) : 0;
}

ULONG GetDoubleClickTime(void)
{
    return (ULONG)WinQuerySysValue(HWND_DESKTOP, SV_DBLCLKTIME);
}
