/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Painting entry points: WinBeginPaint, WinEndPaint, WinFillRect and WinDrawBorder. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"

BOOL WinEndPaint(HPS hps)
{
    TRACE_NATIVE("WinEndPaint(%u)", (uint)hps);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
    {
        return NULLHANDLE;
    }
    PresentationSpace *ps = getPresentationSpaceFromHPS(anchor, hps);
    if (!ps)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INV_HPS, NULLHANDLE);
    }

    Window *win = ps->window;
    if (win && win->heavy.window)
        presentWindow(anchor, win);

    ps->paint_clip_set = FALSE;
    if (!ps->persistent)
        destroyPresentationSpace(anchor, ps);
    return TRUE;
}

void calcHeavyweightRect(const Window *win, LxHostRect *rect)
{
    int x = 0, y = 0;

    rect->w = win->w;
    rect->h = win->h;

    while (win->parent && win->parent != &desktop_window)
    {
        x += win->x;
        y += win->y;
        win = win->parent;
    }

    rect->x = x;
    rect->y = (win->h - y) - rect->h;
}

BOOL WinFillRect(HPS hps, PRECTL prcl, LONG lColor)
{
    TRACE_NATIVE("WinFillRect(%u, %p, %d)", (uint)hps, prcl, (int)lColor);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
    {
        return NULLHANDLE;
    }
    PresentationSpace *ps = getPresentationSpaceFromHPS(anchor, hps);
    if (!ps)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INV_HPS, NULLHANDLE);
    }
    if (!prcl)
        return FALSE;
    Window *win = ps->window;
    if (!win && ps->bitmap_lease.resource)
    {
        PmBitmap *bitmap = ps->bitmap_lease.resource;
        LONG rgb = lx_pm_resolve_color(hps, lColor);
        if (rgb < 0)
            return FALSE;
        LONG left = prcl->xLeft > 0 ? prcl->xLeft : 0;
        LONG bottom = prcl->yBottom > 0 ? prcl->yBottom : 0;
        LONG right = prcl->xRight < (LONG)bitmap->width ? prcl->xRight : (LONG)bitmap->width;
        LONG top = prcl->yTop < (LONG)bitmap->height ? prcl->yTop : (LONG)bitmap->height;
        for (LONG y = bottom; y < top; y++)
            for (LONG x = left; x < right; x++)
                if (lx_pm_clip_point(hps, x, y))
                    bitmap->pixels[(size_t)y * bitmap->width + x] = (ULONG)rgb;
        return TRUE;
    }
    if (!win)
        return FALSE;
    LxHostRect cliprect;
    calcHeavyweightRect(win, &cliprect);

    LxHostRect rect;
    rect.x = cliprect.x + prcl->xLeft;
    rect.y = cliprect.y + (cliprect.h - prcl->yTop);
    rect.w = prcl->xRight - prcl->xLeft;
    rect.h = prcl->yTop - prcl->yBottom;

    uint8 r, g, b;
    LONG rgb = lx_pm_resolve_color(hps, lColor);
    if (rgb < 0)
        return FALSE;
    r = (rgb >> 16) & 255;
    g = (rgb >> 8) & 255;
    b = rgb & 255;

    if (!win->heavy.window)
        return FALSE;
    PmRegion visible = {0};
    if (!lx_pm_window_clip(hps, prcl, &visible))
        return FALSE;
    BOOL result = TRUE;
    for (size_t i = 0; result && i < visible.count; i++)
    {
        RECTL intersection;
        if (!WinIntersectRect(0, &intersection, prcl, &visible.rects[i]))
            continue;
        LxHostRect part = {
            cliprect.x + intersection.xLeft, cliprect.y + cliprect.h - intersection.yTop,
            intersection.xRight - intersection.xLeft, intersection.yTop - intersection.yBottom};
        result = lx_host_window_fill(win->heavy.window, &cliprect, &part, r, g, b);
    }
    free(visible.rects);
    return result;
}

BOOL WinDrawBorder(HPS handle, PRECTL rect, LONG cx, LONG cy, LONG foreground, LONG background,
                   ULONG flags)
{
    TRACE_NATIVE("WinDrawBorder(%u, %p, %d, %d, %d, %d, %u)", (uint)handle, rect, (int)cx, (int)cy,
                 (int)foreground, (int)background, (uint)flags);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !rect || cx < 0 || cy < 0 ||
        (flags & ~(ULONG)(DB_STANDARD | DB_AREAATTRS | DB_INTERIOR | DB_AREAMIXMODE)) ||
        (flags & DB_ROP) > DB_AREAMIXMODE)
        return FALSE;
    int64_t dx = cx, dy = cy;
    if (flags & DB_STANDARD)
    {
        dx *= WinQuerySysValue(HWND_DESKTOP, SV_CXBORDER);
        dy *= WinQuerySysValue(HWND_DESKTOP, SV_CYBORDER);
    }
    PmBitmap *bitmap = ps->bitmap_lease.resource;
    LONG width = bitmap       ? (LONG)bitmap->width
                 : ps->window ? ps->window->w
                              : 0,
         height = bitmap       ? (LONG)bitmap->height
                  : ps->window ? ps->window->h
                               : 0;
    RECTL bounds = {0, 0, width, height}, drawing;
    if (!WinIntersectRect(0, &drawing, rect, &bounds))
        return TRUE;
    uint32_t *pixels = malloc((size_t)(drawing.xRight - drawing.xLeft) * sizeof *pixels);
    if (!pixels)
        return FALSE;
    LONG fore = lx_pm_resolve_color(handle, foreground),
         back = lx_pm_resolve_color(handle, background);
    BOOL ok = (flags & DB_AREAATTRS) || (fore >= 0 && back >= 0);
    LONG operation = flags & DB_ROP;
    /* Primitive bundle arrays are indexed by PRIM_ value minus one. */
    LONG mix = operation == DB_PATINVERT     ? FM_XOR
               : operation == DB_DESTINVERT  ? FM_INVERT
               : operation == DB_AREAMIXMODE ? ps->primitive_mix[PRIM_AREA - 1]
                                             : FM_OVERPAINT;
    for (LONG y = drawing.yBottom; ok && y < drawing.yTop; y++)
    {
        ok = lx_pm_raster_read(handle, drawing.xLeft, y, drawing.xRight - drawing.xLeft, 1, pixels);
        for (LONG x = drawing.xLeft; ok && x < drawing.xRight; x++)
        {
            BOOL interior = (int64_t)x >= (int64_t)rect->xLeft + dx &&
                            (int64_t)x < (int64_t)rect->xRight - dx &&
                            (int64_t)y >= (int64_t)rect->yBottom + dy &&
                            (int64_t)y < (int64_t)rect->yTop - dy;
            if (interior && !(flags & DB_INTERIOR))
                continue;
            uint32_t source;
            BOOL ink = TRUE;
            ok = lx_pm_pattern_pixel(handle, x, y, &source, &ink);
            if (!(flags & DB_AREAATTRS))
                source = (uint32_t)(ink != interior ? fore : back);
            else if (interior)
            {
                LONG swapped =
                    lx_pm_resolve_color(handle, ink ? ps->primitive_back[PRIM_AREA - 1]
                                                    : ps->primitive_color[PRIM_AREA - 1]);
                if (swapped < 0)
                    ok = FALSE;
                source = (uint32_t)swapped;
            }
            if (ok)
                pixels[x - drawing.xLeft] = lx_pm_mix_pixel(source, pixels[x - drawing.xLeft], mix);
        }
        if (ok)
            ok = lx_pm_raster_write(handle, drawing.xLeft, y, drawing.xRight - drawing.xLeft, 1,
                                    pixels);
    }
    free(pixels);
    return ok;
}

HPS WinBeginPaint(HWND hwnd, HPS hps, PRECTL rect)
{
    return acquirePresentationSpace(hwnd, hps, rect, TRUE);
}
