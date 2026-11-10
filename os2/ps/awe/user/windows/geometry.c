/* Public domain. No rights reserved. Wholly AI-authored. */
/* Rectangle and point queries translate PM coordinates at drawable boundaries. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/ps/awe/gdi/dc.h"

enum
{
    /* WindowFromPoint stops descending after this many nested children. */
    WINDOW_NESTING_LIMIT = 256,
    /* Most points converted by one call. */
    POINT_COUNT_LIMIT = 65536
};

BOOL GetClientRect(ULONG window, PVOID output)
{
    RECTL rect;
    if (!output || !open32Presentation() || !WinQueryWindowRect(open32ClientWindow(window), &rect))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    lx_guest_store_u32((uint8 *)output + WIN32_RECT_LEFT, 0);
    lx_guest_store_u32((uint8 *)output + WIN32_RECT_TOP, 0);
    lx_guest_store_u32((uint8 *)output + WIN32_RECT_RIGHT, (ULONG)(rect.xRight - rect.xLeft));
    lx_guest_store_u32((uint8 *)output + WIN32_RECT_BOTTOM, (ULONG)(rect.yTop - rect.yBottom));
    return TRUE;
}

BOOL GetWindowRect(ULONG window, PVOID output)
{
    RECTL rect;
    if (!output || !open32Presentation() || !WinQueryWindowRect(window, &rect))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    POINTL points[2] = {{rect.xLeft, rect.yBottom}, {rect.xRight, rect.yTop}};
    if (!WinMapWindowPoints(window, HWND_DESKTOP, points, 2))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    LONG height = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
    lx_guest_store_u32((uint8 *)output + WIN32_RECT_LEFT, (ULONG)points[0].x);
    lx_guest_store_u32((uint8 *)output + WIN32_RECT_TOP, (ULONG)(height - points[1].y));
    lx_guest_store_u32((uint8 *)output + WIN32_RECT_RIGHT, (ULONG)points[1].x);
    lx_guest_store_u32((uint8 *)output + WIN32_RECT_BOTTOM, (ULONG)(height - points[0].y));
    return TRUE;
}

static BOOL map_point(ULONG window, PVOID point, BOOL to_screen)
{
    window = open32ClientWindow(window);
    RECTL rect;
    if (!point || !open32Presentation() || !WinQueryWindowRect(window, &rect))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    LONG screen = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
    LONG x = (LONG)lx_guest_load_u32((uint8 *)point + WIN32_POINT_X),
         y = (LONG)lx_guest_load_u32((uint8 *)point + WIN32_POINT_Y);
    int64_t converted = (int64_t)(to_screen ? rect.yTop : screen) - y;
    if (converted < INT32_MIN || converted > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    POINTL mapped = {x, (LONG)converted};
    if (!WinMapWindowPoints(to_screen ? window : HWND_DESKTOP, to_screen ? HWND_DESKTOP : window,
                            &mapped, 1))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    converted = (int64_t)(to_screen ? screen : rect.yTop) - mapped.y;
    if (converted < INT32_MIN || converted > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    lx_guest_store_u32((uint8 *)point + WIN32_POINT_X, (ULONG)mapped.x);
    lx_guest_store_u32((uint8 *)point + WIN32_POINT_Y, (ULONG)converted);
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
    if (depth >= WINDOW_NESTING_LIMIT)
        return parent;
    for (ULONG child = WinQueryWindow(parent, QW_TOP); child;
         child = WinQueryWindow(child, QW_NEXT))
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
    LONG width = WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN),
         height = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);
    if (x < 0 || y < 0 || x >= width || y >= height)
        return 0;
    return open32PublicWindow(window_at_point(HWND_DESKTOP, x, height - 1 - y, 0));
}

BOOL IsIconic(ULONG window)
{
    return open32Presentation() && WinIsWindow(open32Presentation(), window) &&
           (WinQueryWindowULong(window, QWL_STYLE) & WS_MINIMIZED) != 0;
}

BOOL IsZoomed(ULONG window)
{
    return open32Presentation() && WinIsWindow(open32Presentation(), window) &&
           (WinQueryWindowULong(window, QWL_STYLE) & WS_MAXIMIZED) != 0;
}

BOOL GetWindowPlacement(ULONG window, PVOID output)
{
    if (!output || lx_guest_load_u32((uint8 *)output + WIN32_WINDOWPLACEMENT_LENGTH) !=
                       WIN32_WINDOWPLACEMENT_SIZE)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SWP position;
    RECTL parent;
    if (!open32Presentation() || !WinQueryWindowPos(window, &position) ||
        !WinQueryWindowRect(WinQueryWindow(window, QW_PARENT), &parent))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    ULONG style = WinQueryWindowULong(window, QWL_STYLE);
    ULONG state = style & WS_MINIMIZED   ? WIN32_SW_SHOWMINIMIZED
                  : style & WS_MAXIMIZED ? WIN32_SW_SHOWMAXIMIZED
                                         : WIN32_SW_SHOWNORMAL;
    LONG x = position.x, y = position.y, width = position.cx, height = position.cy;
    Open32Window *w = open32FindWindow(window);
    if (w && (w->extended_style & WIN32_WS_EX_MDICHILD) &&
        (w->style & ((ULONG)WIN32_WS_MINIMIZE | WIN32_WS_MAXIMIZE)))
    {
        state = w->style & WIN32_WS_MINIMIZE ? WIN32_SW_SHOWMINIMIZED : WIN32_SW_SHOWMAXIMIZED;
        if (w->mdi_normal_valid)
        {
            x = w->mdi_normal_x;
            width = w->mdi_normal_width;
            height = w->mdi_normal_height;
            y = parent.yTop - w->mdi_normal_y - height;
        }
    }
    char name[16];
    /* A minimized or maximized PM frame keeps its normal rectangle in its restore words. */
    if (state != WIN32_SW_SHOWNORMAL && WinQueryClassName(window, sizeof name, name) &&
        !strcmp(name, "#1")) /* PM's frame class */
    {
        x = (SHORT)WinQueryWindowUShort(window, QWS_XRESTORE);
        y = (SHORT)WinQueryWindowUShort(window, QWS_YRESTORE);
        width = WinQueryWindowUShort(window, QWS_CXRESTORE);
        height = WinQueryWindowUShort(window, QWS_CYRESTORE);
    }
    int64_t top = (int64_t)parent.yTop - y - height;
    int64_t right = (int64_t)x + width, bottom = top + height;
    if (top < INT32_MIN || top > INT32_MAX || right < INT32_MIN || right > INT32_MAX ||
        bottom < INT32_MIN || bottom > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* Minimized and maximized positions are -1, the system default. */
    uint32 fields[] = {WIN32_WINDOWPLACEMENT_SIZE,
                       0,
                       state,
                       UINT32_MAX,
                       UINT32_MAX,
                       UINT32_MAX,
                       UINT32_MAX,
                       (ULONG)x,
                       (ULONG)top,
                       (ULONG)right,
                       (ULONG)bottom};
    for (unsigned i = 0; i < sizeof fields / sizeof *fields; i++)
        lx_guest_store_u32((uint8 *)output + i * 4, fields[i]);
    return TRUE;
}

BOOL SetWindowPlacement(ULONG window, PVOID input)
{
    if (!input || lx_guest_load_u32((uint8 *)input + WIN32_WINDOWPLACEMENT_LENGTH) !=
                      WIN32_WINDOWPLACEMENT_SIZE)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const uint8 *wire = input;
    const uint8 *normal = wire + WIN32_WINDOWPLACEMENT_NORMAL_POSITION;
    ULONG flags = lx_guest_load_u32(wire + WIN32_WINDOWPLACEMENT_FLAGS),
          command = lx_guest_load_u32(wire + WIN32_WINDOWPLACEMENT_SHOW_COMMAND);
    BOOL activate = command == WIN32_SW_SHOWNORMAL || command == WIN32_SW_SHOWMAXIMIZED ||
                    command == WIN32_SW_SHOW || command == WIN32_SW_RESTORE ||
                    command == WIN32_SW_SHOWDEFAULT;
    LONG left = (LONG)lx_guest_load_u32(normal + WIN32_RECT_LEFT),
         top = (LONG)lx_guest_load_u32(normal + WIN32_RECT_TOP);
    LONG right = (LONG)lx_guest_load_u32(normal + WIN32_RECT_RIGHT),
         bottom = (LONG)lx_guest_load_u32(normal + WIN32_RECT_BOTTOM);
    int64_t width = (int64_t)right - left, height = (int64_t)bottom - top;
    if ((flags & ~((ULONG)WIN32_WPF_SETMINPOSITION | WIN32_WPF_RESTORETOMAXIMIZED |
                   WIN32_WPF_ASYNCWINDOWPLACEMENT)) ||
        command > WIN32_SW_FORCEMINIMIZE || width <= 0 || width > INT32_MAX || height <= 0 ||
        height > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* Placement records returned by GetWindowPlacement use default min/max
       positions. Explicit icon positioning and restore-to-maximized flags need
       additional retained state; do not silently accept those requests. */
    if (flags & (WIN32_WPF_SETMINPOSITION | WIN32_WPF_RESTORETOMAXIMIZED))
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    SWP position;
    RECTL parent;
    if (!open32Presentation() || !WinQueryWindowPos(window, &position) ||
        !WinQueryWindowRect(WinQueryWindow(window, QW_PARENT), &parent))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    int64_t y = (int64_t)parent.yTop - bottom;
    if (y < INT32_MIN || y > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Open32Window *w = open32FindWindow(window);
    BOOL mdi = w && (w->extended_style & WIN32_WS_EX_MDICHILD);
    ULONG old_state = WinQueryWindowULong(window, QWL_STYLE) & (WS_MINIMIZED | WS_MAXIMIZED);
    ULONG old_show = old_state & WS_MINIMIZED ? WIN32_SW_SHOWMINIMIZED
                     : old_state              ? WIN32_SW_SHOWMAXIMIZED
                                              : WIN32_SW_SHOWNORMAL;
    if (mdi)
    {
        old_state = w->style & ((ULONG)WIN32_WS_MINIMIZE | WIN32_WS_MAXIMIZE);
        old_show = old_state & WIN32_WS_MINIMIZE ? WIN32_SW_SHOWMINIMIZED
                   : old_state                   ? WIN32_SW_SHOWMAXIMIZED
                                                 : WIN32_SW_SHOWNORMAL;
    }
    /* SW_SHOW/SW_SHOWNA retain the current state; SW_SHOWDEFAULT uses normal
       placement because there is no separate host startup-info show command. */
    if (command == WIN32_SW_SHOW || command == WIN32_SW_SHOWNA)
        command = old_state ? old_show : command;
    if (command == WIN32_SW_SHOWDEFAULT)
        command = WIN32_SW_SHOWNORMAL;
    BOOL minimize = command == WIN32_SW_SHOWMINIMIZED || command == WIN32_SW_MINIMIZE ||
                    command == WIN32_SW_SHOWMINNOACTIVE || command == WIN32_SW_FORCEMINIMIZE;
    char class_name[16];
    BOOL frame = WinQueryClassName(window, sizeof class_name, class_name) &&
                 !strcmp(class_name, "#1"); /* PM's frame class */
    if ((minimize || command == WIN32_SW_MAXIMIZE) && (!frame && (!mdi || minimize)))
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (frame && (minimize || command == WIN32_SW_MAXIMIZE) &&
        (left < INT16_MIN || left > INT16_MAX || y < INT16_MIN || y > INT16_MAX ||
         width > UINT16_MAX || height > UINT16_MAX))
    {
        /* PM frame restore words cannot retain a larger normal rectangle. */
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (old_state)
    {
        if (mdi)
            ShowWindow(window, WIN32_SW_RESTORE);
        else if (!WinSetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_RESTORE))
            return FALSE;
    }
    /* Set normal geometry before applying min/max so PM and MDI both save the
       requested restore rectangle rather than their previous on-screen size. */
    BOOL moved = w ? SetWindowPos(window, WIN32_HWND_TOP, left, top, (LONG)width, (LONG)height,
                                  WIN32_SWP_NOZORDER | WIN32_SWP_NOACTIVATE)
                   : WinSetWindowPos(window, HWND_TOP, left, (LONG)y, (LONG)width, (LONG)height,
                                     SWP_MOVE | SWP_SIZE);
    if (!moved)
        return FALSE;
    if (mdi)
    {
        ShowWindow(window, (LONG)command);
        return WinIsWindow(0, window);
    }
    ULONG show = command != WIN32_SW_HIDE ? SWP_SHOW : SWP_HIDE;
    if (minimize)
        show |= SWP_MINIMIZE;
    else if (command == WIN32_SW_MAXIMIZE)
        show |= SWP_MAXIMIZE;
    if (activate)
        show |= SWP_ACTIVATE;
    BOOL result = WinSetWindowPos(window, HWND_TOP, 0, 0, 0, 0, show);
    w = open32FindWindow(window);
    if (result && w)
        w->style = command != WIN32_SW_HIDE ? w->style | WIN32_WS_VISIBLE
                                            : w->style & ~(ULONG)WIN32_WS_VISIBLE;
    return result;
}

ULONG GetParent(ULONG window)
{
    if (!open32Presentation() || !WinIsWindow(open32Presentation(), window))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    Open32Window *w = open32FindWindow(window);
    if (w)
        return (w->style & ((ULONG)WIN32_WS_CHILD | WIN32_WS_POPUP)) ? w->parent : 0;
    ULONG parent = WinQueryWindow(window, QW_PARENT);
    return parent == HWND_DESKTOP ? WinQueryWindow(window, QW_OWNER) : parent;
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
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (count < 0 || count > POINT_COUNT_LIMIT || (count && !points))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    uint8 *wire = points;
    for (LONG i = 0; i < count; i++)
    {
        int64_t y = (int64_t)height - 1 -
                    (LONG)lx_guest_load_u32(wire + i * WIN32_POINT_SIZE + WIN32_POINT_Y);
        if (y < INT32_MIN || y > INT32_MAX)
        {
            open32SetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
    }
    for (LONG i = 0; i < count; i++)
        lx_guest_store_u32(
            wire + i * WIN32_POINT_SIZE + WIN32_POINT_Y,
            (ULONG)((int64_t)height - 1 -
                    (LONG)lx_guest_load_u32(wire + i * WIN32_POINT_SIZE + WIN32_POINT_Y)));
    return TRUE;
}

ULONG SetParent(ULONG window, ULONG parent)
{
    HAB hab = open32Presentation();
    HWND target = parent ? open32ClientWindow(parent) : HWND_DESKTOP;
    if (!hab || !WinIsWindow(hab, window) || !WinIsWindow(hab, target))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    ULONG previous = WinQueryWindow(window, QW_PARENT);
    if (!WinSetParent(window, target, TRUE))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    Open32Window *w = open32FindWindow(window);
    if (w)
    {
        w->parent = parent;
        /* Reapply the top-left position using the new PM parent's height. */
        SetWindowPos(window, WIN32_HWND_TOP, w->x, w->y, 0, 0,
                     WIN32_SWP_NOSIZE | WIN32_SWP_NOZORDER | WIN32_SWP_NOACTIVATE);
    }
    return previous == HWND_DESKTOP ? 0 : open32PublicWindow(previous);
}

LONG MapWindowPoints(ULONG from, ULONG to, PVOID points, ULONG count)
{
    if (!points || !count || count > POINT_COUNT_LIMIT)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    uint8 origin[WIN32_POINT_SIZE] = {0};
    if (from && !ClientToScreen(from, origin))
        return 0;
    if (to && !ScreenToClient(to, origin))
        return 0;
    LONG dx = (LONG)lx_guest_load_u32(origin + WIN32_POINT_X),
         dy = (LONG)lx_guest_load_u32(origin + WIN32_POINT_Y);
    for (ULONG i = 0; i < count; i++)
    {
        uint8 *point = (uint8 *)points + i * WIN32_POINT_SIZE;
        lx_guest_store_u32(point + WIN32_POINT_X,
                           lx_guest_load_u32(point + WIN32_POINT_X) + (ULONG)dx);
        lx_guest_store_u32(point + WIN32_POINT_Y,
                           lx_guest_load_u32(point + WIN32_POINT_Y) + (ULONG)dy);
    }
    return (LONG)((ULONG)(USHORT)dx | (ULONG)(USHORT)dy << 16);
}
