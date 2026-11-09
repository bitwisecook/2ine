/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/private/pmp.h"

static void wire_rect(uint8 p[16], LONG l, LONG t, LONG r, LONG b)
{
    lx_guest_store_u32(p, (ULONG)l);
    lx_guest_store_u32(p + 4, (ULONG)t);
    lx_guest_store_u32(p + 8, (ULONG)r);
    lx_guest_store_u32(p + 12, (ULONG)b);
}

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
            LONG a = (LONG)lx_guest_load_u32(p), c = (LONG)lx_guest_load_u32(p + 4),
                 d = (LONG)lx_guest_load_u32(p + 8), e = (LONG)lx_guest_load_u32(p + 12);
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
            uint32 *pixels = area <= 16777216 ? malloc(area * 4) : NULL;
            ok = pixels &&
                 lx_pm_raster_read(h, il - dx, open32DCHeight(h) - (ib - dy), (unsigned)(ir - il),
                                   (unsigned)(ib - it), pixels) &&
                 lx_pm_raster_write(h, il, open32DCHeight(h) - ib, (unsigned)(ir - il),
                                    (unsigned)(ib - it), pixels);
            free(pixels);
        }
        ULONG retained = CreateRectRgn(il, it, ir, ib);
        ok = ok && retained && SetRectRgn(region, l, t, r, b) &&
             CombineRgn(region, region, retained, 4);
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
                LONG a = (LONG)lx_guest_load_u32(p), c = (LONG)lx_guest_load_u32(p + 4),
                     d = (LONG)lx_guest_load_u32(p + 8), e = (LONG)lx_guest_load_u32(p + 12);
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
        uint8 bounds[16];
        result = GetRgnBox(region, bounds);
        if (flags & 2)
            ok = InvalidateRgn(window, region, (flags & 4) != 0);
    }
    if (ok && (flags & 1))
    {
        HWND child = GetWindow(window, 5);
        while (child)
        {
            uint8 rect[16];
            if (GetWindowRect(child, rect))
            {
                LONG x = (LONG)lx_guest_load_u32(rect), y = (LONG)lx_guest_load_u32(rect + 4);
                uint8 point[8];
                lx_guest_store_u32(point, (ULONG)x);
                lx_guest_store_u32(point + 4, (ULONG)y);
                if (ScreenToClient(window, point))
                {
                    int64_t nx = (int64_t)(LONG)lx_guest_load_u32(point) + dx,
                            ny = (int64_t)(LONG)lx_guest_load_u32(point + 4) + dy;
                    if (nx >= INT32_MIN && nx <= INT32_MAX && ny >= INT32_MIN && ny <= INT32_MAX)
                        SetWindowPos(child, 0, (LONG)nx, (LONG)ny, 0, 0, 0x15);
                }
            }
            child = GetWindow(child, 2);
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
    return ScrollWindowEx(window, dx, dy, scroll, clip, 0, NULL, scroll ? 2 : 3) != 0;
}
