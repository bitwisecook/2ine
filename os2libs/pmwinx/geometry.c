/* Public domain. No rights reserved. Wholly AI-authored. */
/* Rectangle and point queries translate PM coordinates at drawable boundaries. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/drawing.h"

BOOL GetClientRect(ULONG window, PVOID output)
{
    RECTL rect;
    if (!output || !open32Presentation() || !WinQueryWindowRect(open32ClientWindow(window), &rect))
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
    window = open32ClientWindow(window);
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

static ULONG window_at_point(ULONG parent, LONG x, LONG y, unsigned depth)
{
    if (depth >= 256)
        return parent;
    for (ULONG child = WinQueryWindow(parent, 2); child; child = WinQueryWindow(child, 0))
    {
        SWP position;
        if (!WinIsWindowVisible(child) || !WinIsWindowEnabled(child) ||
            !WinQueryWindowPos(child, &position) || x < position.x || y < position.y ||
            (int64_t)x >= (int64_t)position.x + position.cx ||
            (int64_t)y >= (int64_t)position.y + position.cy)
            continue;
        return window_at_point(child, x - position.x, y - position.y, depth + 1);
    }
    return parent;
}

ULONG WindowFromPoint(LONG x, LONG y)
{
    TRACE_NATIVE("WindowFromPoint(%d, %d)", x, y);
    if (!open32Presentation())
        return 0;
    LONG width = WinQuerySysValue(HWND_DESKTOP, 20), height = WinQuerySysValue(HWND_DESKTOP, 21);
    if (x < 0 || y < 0 || x >= width || y >= height)
        return 0;
    return open32PublicWindow(window_at_point(HWND_DESKTOP, x, height - 1 - y, 0));
}

BOOL IsIconic(ULONG window)
{
    return open32Presentation() && WinIsWindow(open32Presentation(), window) &&
           (WinQueryWindowULong(window, -2) & WS_MINIMIZED) != 0;
}

BOOL IsZoomed(ULONG window)
{
    return open32Presentation() && WinIsWindow(open32Presentation(), window) &&
           (WinQueryWindowULong(window, -2) & WS_MAXIMIZED) != 0;
}

BOOL GetWindowPlacement(ULONG window, PVOID output)
{
    if (!output || lx_guest_load_u32(output) != 44)
    {
        open32SetError(87);
        return FALSE;
    }
    SWP position;
    RECTL parent;
    if (!open32Presentation() || !WinQueryWindowPos(window, &position) ||
        !WinQueryWindowRect(WinQueryWindow(window, 5), &parent))
    {
        open32SetError(1400);
        return FALSE;
    }
    ULONG style = WinQueryWindowULong(window, -2);
    ULONG state = style & WS_MINIMIZED ? 2 : style & WS_MAXIMIZED ? 3 : 1;
    LONG x = position.x, y = position.y, width = position.cx, height = position.cy;
    Open32Window *w = open32FindWindow(window);
    if (w && (w->extended_style & 0x40) && (w->style & 0x21000000))
    {
        state = w->style & 0x20000000 ? 2 : 3;
        if (w->mdi_normal_valid)
        {
            x = w->mdi_normal_x;
            width = w->mdi_normal_width;
            height = w->mdi_normal_height;
            y = parent.yTop - w->mdi_normal_y - height;
        }
    }
    char name[16];
    if (state != 1 && WinQueryClassName(window, sizeof name, name) && !strcmp(name, "#1"))
    {
        x = (SHORT)WinQueryWindowUShort(window, 0x0c);
        y = (SHORT)WinQueryWindowUShort(window, 0x0e);
        width = WinQueryWindowUShort(window, 0x10);
        height = WinQueryWindowUShort(window, 0x12);
    }
    int64_t top = (int64_t)parent.yTop - y - height;
    int64_t right = (int64_t)x + width, bottom = top + height;
    if (top < INT32_MIN || top > INT32_MAX || right < INT32_MIN || right > INT32_MAX ||
        bottom < INT32_MIN || bottom > INT32_MAX)
    {
        open32SetError(87);
        return FALSE;
    }
    uint32 fields[] = {44,         0,        state,      UINT32_MAX,   UINT32_MAX,   UINT32_MAX,
                       UINT32_MAX, (ULONG)x, (ULONG)top, (ULONG)right, (ULONG)bottom};
    for (unsigned i = 0; i < sizeof fields / sizeof *fields; i++)
        lx_guest_store_u32((uint8 *)output + i * 4, fields[i]);
    return TRUE;
}

BOOL SetWindowPlacement(ULONG window, PVOID input)
{
    if (!input || lx_guest_load_u32(input) != 44)
    {
        open32SetError(87);
        return FALSE;
    }
    const uint8 *wire = input;
    ULONG flags = lx_guest_load_u32(wire + 4), command = lx_guest_load_u32(wire + 8);
    BOOL activate = command == 1 || command == 3 || command == 5 || command == 9 || command == 10;
    LONG left = (LONG)lx_guest_load_u32(wire + 28), top = (LONG)lx_guest_load_u32(wire + 32);
    LONG right = (LONG)lx_guest_load_u32(wire + 36), bottom = (LONG)lx_guest_load_u32(wire + 40);
    int64_t width = (int64_t)right - left, height = (int64_t)bottom - top;
    if ((flags & ~7u) || command > 11 || width <= 0 || width > INT32_MAX || height <= 0 ||
        height > INT32_MAX)
    {
        open32SetError(87);
        return FALSE;
    }
    /* Placement records returned by GetWindowPlacement use default min/max
       positions. Explicit icon positioning and restore-to-maximized flags need
       additional retained state; do not silently accept those requests. */
    if (flags & 3)
    {
        open32SetError(50);
        return FALSE;
    }
    SWP position;
    RECTL parent;
    if (!open32Presentation() || !WinQueryWindowPos(window, &position) ||
        !WinQueryWindowRect(WinQueryWindow(window, 5), &parent))
    {
        open32SetError(1400);
        return FALSE;
    }
    int64_t y = (int64_t)parent.yTop - bottom;
    if (y < INT32_MIN || y > INT32_MAX)
    {
        open32SetError(87);
        return FALSE;
    }
    Open32Window *w = open32FindWindow(window);
    BOOL mdi = w && (w->extended_style & 0x40);
    ULONG old_state = WinQueryWindowULong(window, -2) & (WS_MINIMIZED | WS_MAXIMIZED);
    ULONG old_show = old_state & WS_MINIMIZED ? 2 : old_state ? 3 : 1;
    if (mdi)
    {
        old_state = w->style & 0x21000000;
        old_show = old_state & 0x20000000 ? 2 : old_state ? 3 : 1;
    }
    /* SW_SHOW/SW_SHOWNA retain the current state; SW_SHOWDEFAULT uses normal
       placement because there is no separate host startup-info show command. */
    if (command == 5 || command == 8)
        command = old_state ? old_show : command;
    if (command == 10)
        command = 1;
    BOOL minimize = command == 2 || command == 6 || command == 7 || command == 11;
    char class_name[16];
    BOOL frame =
        WinQueryClassName(window, sizeof class_name, class_name) && !strcmp(class_name, "#1");
    if ((minimize || command == 3) && (!frame && (!mdi || minimize)))
    {
        open32SetError(50);
        return FALSE;
    }
    if (frame && (minimize || command == 3) &&
        (left < INT16_MIN || left > INT16_MAX || y < INT16_MIN || y > INT16_MAX ||
         width > UINT16_MAX || height > UINT16_MAX))
    {
        /* PM frame restore words cannot retain a larger normal rectangle. */
        open32SetError(87);
        return FALSE;
    }
    if (old_state)
    {
        if (mdi)
            ShowWindow(window, 9);
        else if (!WinSetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_RESTORE))
            return FALSE;
    }
    /* Set normal geometry before applying min/max so PM and MDI both save the
       requested restore rectangle rather than their previous on-screen size. */
    BOOL moved = w ? SetWindowPos(window, 0, left, top, (LONG)width, (LONG)height, 4 | 16)
                   : WinSetWindowPos(window, HWND_TOP, left, (LONG)y, (LONG)width, (LONG)height,
                                     SWP_MOVE | SWP_SIZE);
    if (!moved)
        return FALSE;
    if (mdi)
    {
        ShowWindow(window, (LONG)command);
        return WinIsWindow(0, window);
    }
    ULONG show = command ? SWP_SHOW : SWP_HIDE;
    if (minimize)
        show |= SWP_MINIMIZE;
    else if (command == 3)
        show |= SWP_MAXIMIZE;
    if (activate)
        show |= SWP_ACTIVATE;
    BOOL result = WinSetWindowPos(window, HWND_TOP, 0, 0, 0, 0, show);
    w = open32FindWindow(window);
    if (result && w)
        w->style = command ? w->style | 0x10000000 : w->style & ~0x10000000u;
    return result;
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
    window = open32ClientWindow(window);
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
    HWND target = parent ? open32ClientWindow(parent) : HWND_DESKTOP;
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
    return previous == HWND_DESKTOP ? 0 : open32PublicWindow(previous);
}
