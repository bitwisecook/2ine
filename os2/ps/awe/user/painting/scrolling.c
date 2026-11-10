/* Public domain. No rights reserved. Wholly AI-authored. */
/* ScrollDC, ScrollWindow and ScrollWindowEx: move pixels through the PM raster and
   carry pending update rectangles and child windows with them. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/private/pmp.h"

enum
{
    /* Largest area copied through a scratch pixel buffer: 16 megapixels. */
    SCROLL_PIXELS_MAX = 16 * 1024 * 1024
};

BOOL ScrollDC(ULONG h, LONG dx, LONG dy, PVOID scroll, PVOID clip, ULONG update, PVOID box)
{
    if (!open32FindDC(h))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    Open32DC *mapping = open32FindDC(h);
    LONG l = 0, t = 0, r = open32DCWidth(h), b = open32DCHeight(h);
    PVOID inputs[] = {scroll, clip};
    for (unsigned i = 0; i < 2; i++)
        if (inputs[i])
        {
            uint8 *p = inputs[i];
            LONG a = (LONG)lx_guest_load_u32(p + WIN32_RECT_LEFT),
                 c = (LONG)lx_guest_load_u32(p + WIN32_RECT_TOP),
                 d = (LONG)lx_guest_load_u32(p + WIN32_RECT_RIGHT),
                 e = (LONG)lx_guest_load_u32(p + WIN32_RECT_BOTTOM);
            if (!open32MapPoint(mapping, &a, &c, TRUE) || !open32MapPoint(mapping, &d, &e, TRUE))
                return FALSE;
            if (a > d)
            {
                LONG swap = a;
                a = d;
                d = swap;
            }
            if (c > e)
            {
                LONG swap = c;
                c = e;
                e = swap;
            }
            if (a > l)
                l = a;
            if (c > t)
                t = c;
            if (d < r)
                r = d;
            if (e < b)
                b = e;
        }
    ULONG region = update ? update : CreateRectRgn(0, 0, 0, 0);
    if (!region)
        return FALSE;
    BOOL ok = TRUE;
    if (l >= r || t >= b || (!dx && !dy))
        ok = SetRectRgn(region, 0, 0, 0, 0);
    else
    {
        int64_t rl = (int64_t)l + dx, rr = (int64_t)r + dx, rt = (int64_t)t + dy,
                rb = (int64_t)b + dy;
        LONG il = rl > l ? (rl > r ? r : (LONG)rl) : l, ir = rr < r ? (rr < l ? l : (LONG)rr) : r,
             it = rt > t ? (rt > b ? b : (LONG)rt) : t, ib = rb < b ? (rb < t ? t : (LONG)rb) : b;
        if (il < ir && it < ib)
        {
            size_t area = (size_t)(ir - il) * (size_t)(ib - it);
            uint32 *pixels = area <= SCROLL_PIXELS_MAX ? malloc(area * sizeof(uint32)) : NULL;
            ok = pixels &&
                 lx_pm_raster_read(h, il - dx, open32DCHeight(h) - (ib - dy), (unsigned)(ir - il),
                                   (unsigned)(ib - it), pixels) &&
                 lx_pm_raster_write(h, il, open32DCHeight(h) - ib, (unsigned)(ir - il),
                                    (unsigned)(ib - it), pixels);
            free(pixels);
        }
        ULONG retained = CreateRectRgn(il, it, ir, ib);
        ok = ok && retained && SetRectRgn(region, l, t, r, b) &&
             CombineRgn(region, region, retained, WIN32_RGN_DIFF);
        if (retained)
            DeleteObject(retained);
    }
    if (ok && box)
        ok = GetRgnBox(region, box) != 0;
    if (!update)
        DeleteObject(region);
    if (!ok)
        open32SetError(ERROR_INVALID_PARAMETER);
    return ok;
}

LONG ScrollWindowEx(ULONG window, LONG dx, LONG dy, PVOID scroll, PVOID clip, ULONG update,
                    PVOID box, ULONG flags)
{
    ULONG dc = GetDC(window);
    if (!dc)
        return 0;
    ULONG region = update ? update : CreateRectRgn(0, 0, 0, 0);
    BOOL ok = region && ScrollDC(dc, dx, dy, scroll, clip, region, box);
    LONG result = 0;
    if (ok)
    {
        /* Pending update rectangles travel with the pixels they describe. */
        LONG height = open32DCHeight(dc), l = 0, t = 0, r = open32DCWidth(dc), b = height;
        PVOID inputs[] = {scroll, clip};
        for (unsigned i = 0; i < 2; i++)
            if (inputs[i])
            {
                const uint8 *p = inputs[i];
                LONG a = (LONG)lx_guest_load_u32(p + WIN32_RECT_LEFT),
                     c = (LONG)lx_guest_load_u32(p + WIN32_RECT_TOP),
                     d = (LONG)lx_guest_load_u32(p + WIN32_RECT_RIGHT),
                     e = (LONG)lx_guest_load_u32(p + WIN32_RECT_BOTTOM);
                l = a > l ? a : l;
                t = c > t ? c : t;
                r = d < r ? d : r;
                b = e < b ? e : b;
            }
        RECTL area = {l, height - b, r, height - t};
        ok = l >= r || t >= b ||
             lx_pm_window_scroll_damage(open32ClientWindow(window), &area, dx, -dy);
    }
    if (ok)
    {
        uint8 bounds[WIN32_RECT_SIZE];
        result = GetRgnBox(region, bounds);
        if (flags & WIN32_SW_INVALIDATE)
            ok = InvalidateRgn(window, region, (flags & WIN32_SW_ERASE) != 0);
    }
    if (ok && (flags & WIN32_SW_SCROLLCHILDREN))
    {
        HWND child = GetWindow(window, WIN32_GW_CHILD);
        while (child)
        {
            uint8 rect[WIN32_RECT_SIZE];
            if (GetWindowRect(child, rect))
            {
                LONG x = (LONG)lx_guest_load_u32(rect + WIN32_RECT_LEFT),
                     y = (LONG)lx_guest_load_u32(rect + WIN32_RECT_TOP);
                uint8 point[WIN32_POINT_SIZE];
                lx_guest_store_u32(point + WIN32_POINT_X, (ULONG)x);
                lx_guest_store_u32(point + WIN32_POINT_Y, (ULONG)y);
                if (ScreenToClient(window, point))
                {
                    int64_t nx = (int64_t)(LONG)lx_guest_load_u32(point + WIN32_POINT_X) + dx,
                            ny = (int64_t)(LONG)lx_guest_load_u32(point + WIN32_POINT_Y) + dy;
                    if (nx >= INT32_MIN && nx <= INT32_MAX && ny >= INT32_MIN && ny <= INT32_MAX)
                        SetWindowPos(child, WIN32_HWND_TOP, (LONG)nx, (LONG)ny, 0, 0,
                                     WIN32_SWP_NOSIZE | WIN32_SWP_NOZORDER | WIN32_SWP_NOACTIVATE);
                }
            }
            child = GetWindow(child, WIN32_GW_HWNDNEXT);
        }
    }
    if (region && !update)
        DeleteObject(region);
    ReleaseDC(window, dc);
    return ok ? result : 0;
}

BOOL ScrollWindow(ULONG window, LONG dx, LONG dy, PVOID scroll, PVOID clip)
{
    /* Child windows move only when the whole client area scrolls. */
    return ScrollWindowEx(window, dx, dy, scroll, clip, 0, NULL,
                          scroll ? WIN32_SW_INVALIDATE
                                 : WIN32_SW_INVALIDATE | WIN32_SW_SCROLLCHILDREN) != 0;
}
