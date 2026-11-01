/* Public domain. No rights reserved. Wholly AI-authored. */
/* Rectangle and point queries translate PM coordinates at drawable boundaries. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/drawing.h"
BOOL GetClientRect(ULONG window, PVOID output)
{
    RECTL rect;
    if (!output || !open32Presentation() || !WinQueryWindowRect(window, &rect))
    {
        open32SetError(1400);
        return FALSE;
    }
    lx_guest_store_u32(output, 0);
    lx_guest_store_u32((uint8 *)output + 4, 0);
    lx_guest_store_u32((uint8 *)output + 8, (ULONG)(rect.xRight - rect.xLeft));
    lx_guest_store_u32((uint8 *)output + 12, (ULONG)(rect.yTop - rect.yBottom));
    return TRUE;
}
BOOL GetWindowRect(ULONG window, PVOID output)
{
    RECTL rect;
    if (!output || !open32Presentation() || !WinQueryWindowRect(window, &rect))
    {
        open32SetError(1400);
        return FALSE;
    }
    POINTL points[2] = {{rect.xLeft, rect.yBottom}, {rect.xRight, rect.yTop}};
    if (!WinMapWindowPoints(window, HWND_DESKTOP, points, 2))
    {
        open32SetError(1400);
        return FALSE;
    }
    LONG height = WinQuerySysValue(HWND_DESKTOP, 21);
    lx_guest_store_u32(output, (ULONG)points[0].x);
    lx_guest_store_u32((uint8 *)output + 4, (ULONG)(height - points[1].y));
    lx_guest_store_u32((uint8 *)output + 8, (ULONG)points[1].x);
    lx_guest_store_u32((uint8 *)output + 12, (ULONG)(height - points[0].y));
    return TRUE;
}
static BOOL map_point(ULONG window, PVOID point, BOOL to_screen)
{
    RECTL rect;
    if (!point || !open32Presentation() || !WinQueryWindowRect(window, &rect))
    {
        open32SetError(1400);
        return FALSE;
    }
    LONG screen = WinQuerySysValue(HWND_DESKTOP, 21);
    LONG x = (LONG)lx_guest_load_u32(point), y = (LONG)lx_guest_load_u32((uint8 *)point + 4);
    int64_t converted = (int64_t)(to_screen ? rect.yTop : screen) - y;
    if (converted < INT32_MIN || converted > INT32_MAX)
    {
        open32SetError(87);
        return FALSE;
    }
    POINTL mapped = {x, (LONG)converted};
    if (!WinMapWindowPoints(to_screen ? window : HWND_DESKTOP, to_screen ? HWND_DESKTOP : window,
                            &mapped, 1))
    {
        open32SetError(1400);
        return FALSE;
    }
    converted = (int64_t)(to_screen ? screen : rect.yTop) - mapped.y;
    if (converted < INT32_MIN || converted > INT32_MAX)
    {
        open32SetError(87);
        return FALSE;
    }
    lx_guest_store_u32(point, (ULONG)mapped.x);
    lx_guest_store_u32((uint8 *)point + 4, (ULONG)converted);
    return TRUE;
}
BOOL ClientToScreen(ULONG window, PVOID point)
{
    return map_point(window, point, TRUE);
}
BOOL ScreenToClient(ULONG window, PVOID point)
{
    return map_point(window, point, FALSE);
}
ULONG GetDesktopWindow(void)
{
    return open32Presentation() ? HWND_DESKTOP : 0;
}

BOOL IsIconic(ULONG window)
{
    return open32Presentation() && WinIsWindow(open32Presentation(), window) &&
           (WinQueryWindowULong(window, -2) & WS_MINIMIZED) != 0;
}

ULONG GetParent(ULONG window)
{
    if (!open32Presentation() || !WinIsWindow(open32Presentation(), window))
    {
        open32SetError(1400);
        return 0;
    }
    Open32Window *w = open32FindWindow(window);
    if (w)
        return (w->style & (0x40000000u | 0x80000000u)) ? w->parent : 0;
    ULONG parent = WinQueryWindow(window, 5);
    return parent == HWND_DESKTOP ? WinQueryWindow(window, 4) : parent;
}

BOOL WinTranslateDevicePoints(ULONG dc, ULONG window, PVOID points, LONG count, LONG direction)
{
    TRACE_NATIVE("WinTranslateDevicePoints(%u, %u, %p, %d, %d)", dc, window, points, count,
                 direction);
    /* WINX2PM and PM2WINX use the same involution in the original adapter. */
    (void)direction;
    LONG height;
    RECTL rectangle;
    if (dc && open32FindDC(dc))
        height = open32DCHeight(dc);
    else if (!dc && window && open32Presentation() && WinQueryWindowRect(window, &rectangle))
        height = rectangle.yTop - rectangle.yBottom;
    else
    {
        open32SetError(87);
        return FALSE;
    }
    if (count < 0 || count > 65536 || (count && !points))
    {
        open32SetError(87);
        return FALSE;
    }
    uint8 *wire = points;
    for (LONG i = 0; i < count; i++)
    {
        int64_t y = (int64_t)height - 1 - (LONG)lx_guest_load_u32(wire + i * 8 + 4);
        if (y < INT32_MIN || y > INT32_MAX)
        {
            open32SetError(87);
            return FALSE;
        }
    }
    for (LONG i = 0; i < count; i++)
        lx_guest_store_u32(wire + i * 8 + 4, (ULONG)((int64_t)height - 1 -
                                                     (LONG)lx_guest_load_u32(wire + i * 8 + 4)));
    return TRUE;
}

ULONG SetParent(ULONG window, ULONG parent)
{
    HAB hab = open32Presentation();
    HWND target = parent ? parent : HWND_DESKTOP;
    if (!hab || !WinIsWindow(hab, window) || !WinIsWindow(hab, target))
    {
        open32SetError(1400);
        return 0;
    }
    ULONG previous = WinQueryWindow(window, 5);
    if (!WinSetParent(window, target, TRUE))
    {
        open32SetError(87);
        return 0;
    }
    Open32Window *w = open32FindWindow(window);
    if (w)
    {
        w->parent = parent;
        /* Reapply the top-left position using the new PM parent's height. */
        SetWindowPos(window, 0, w->x, w->y, 0, 0, 1 | 4 | 16);
    }
    return previous == HWND_DESKTOP ? 0 : previous;
}
