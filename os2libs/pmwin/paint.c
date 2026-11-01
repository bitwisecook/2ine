/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
PresentationSpace *getPresentationSpaceFromHPS(const AnchorBlock *anchor, const HPS hps)
{
    if (hps >= FIRST_HPS_VALUE)
    {
        const size_t idx = (size_t)(hps - FIRST_HPS_VALUE);
        if (idx >= anchor->pres_spaces_array_len)
        {
            return NULL;
        }
        PresentationSpace *ps = &anchor->pres_spaces[idx];
        return (ps->hps == hps) ? ps : NULL;
    }
    return NULL;
}

void destroyPresentationSpace(AnchorBlock *anchor, PresentationSpace *ps)
{
    if (!ps || (ps->hps == NULLHANDLE))
    {
        return;
    }
    while (ps->saved)
    {
        PmSavedSpace *saved = ps->saved;
        ps->saved = saved->next;
        free(saved);
    }
    if (ps->clip_lease.resource)
        ((PmRegion *)ps->clip_lease.resource)->selected--;
    lx_handle_release(&ps->clip_lease);
    if (ps->bitmap_lease.resource)
        ((PmBitmap *)ps->bitmap_lease.resource)->selected--;
    lx_handle_release(&ps->bitmap_lease);
    for (unsigned i = 0; i < 255; i++)
        lx_handle_release(&ps->bitmap_ids[i]);
    lx_host_font_close(ps->font);
    if (ps->dc_lease.resource)
        ((PmDeviceContext *)ps->dc_lease.resource)->spaces--;
    lx_handle_release(&ps->dc_lease);
    memset(ps, '\0', sizeof(*ps));
}

HPS acquirePresentationSpace(HWND hwnd, HPS hps, PRECTL prclPaint, BOOL paint)
{
    TRACE_NATIVE("WinBeginPaint(%u, %u, %p)", (uint)hwnd, (uint)hps, prclPaint);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
    {
        return NULLHANDLE;
    }
    Window *win = getWindowFromHWND(anchor, hwnd);
    if (!win)
    {
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, NULLHANDLE);
    }
    for (Window *parent = win; parent; parent = parent->parent)
        if (parent == &anchor->object_window)
        {
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INV_HPS, NULLHANDLE);
        }

    BOOL fresh = !hps;
    PresentationSpace *ps = NULL;
    if (hps != NULLHANDLE)
    {
        ps = getPresentationSpaceFromHPS(anchor, hps);
        if (!ps)
        {
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INV_HPS, NULLHANDLE);
        }
        FIXME("check if this is already assigned to this drawable");
    }
    else
    {

        FIXME("optimize this");
        FIXME("cut-and-paste from WinCreateWindow");
        for (size_t i = 0; i < anchor->pres_spaces_array_len; i++)
        {
            if (anchor->pres_spaces[i].hps == NULLHANDLE)
            {
                ps = &anchor->pres_spaces[i];
                break;
            }
        }

        if (!ps)
        {
            const size_t len = anchor->pres_spaces_array_len;
            void *ptr = realloc(anchor->pres_spaces, sizeof(PresentationSpace) * len * 2);
            if (!ptr)
            {
                SET_WIN_ERROR_AND_RETURN(anchor, PMERR_HEAP_OUT_OF_MEMORY, NULLHANDLE);
            }
            anchor->pres_spaces = (PresentationSpace *)ptr;
            memset(&anchor->pres_spaces[len], '\0', sizeof(PresentationSpace) * len);
            ps = &anchor->pres_spaces[len];
            anchor->pres_spaces_array_len *= 2;
        }
        hps = (HPS)(((size_t)(ps - anchor->pres_spaces)) + FIRST_HPS_VALUE);
    }

    ps->hps = hps;
    ps->window = win;
    ps->dc = hwnd;
    if (fresh)
    {
        ps->codepage = 437;
        ps->color = CLR_BLACK;
        ps->back_color = CLR_WHITE;
        ps->mix = 2;
        ps->back_mix = 5;
        for (unsigned i = 0; i < 5; i++)
        {
            ps->primitive_color[i] = CLR_BLACK;
            ps->primitive_back[i] = CLR_WHITE;
            ps->primitive_mix[i] = 2;
            ps->primitive_back_mix[i] = 5;
        }
        ps->line_type = 7;
        ps->pattern = 16;
        ps->viewport = (RECTL){0, 0, win->w, win->h};
    }

    if (prclPaint)
    {
        FIXME("report the update region, not the whole window");
        *prclPaint =
            WinIsRectEmpty(0, &win->invalid) ? (RECTL){0, 0, win->w, win->h} : win->invalid;
    }

    if (paint)
        validateWindowPaint(anchor, win);
    return hps;
}

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
        lx_host_window_present(win->heavy.window);

    if (!ps->persistent)
        destroyPresentationSpace(anchor, ps);
    return TRUE;
}

void os2ColorIndexToRGB(const LONG os2color, uint8 *r, uint8 *g, uint8 *b)
{
    assert(r != NULL);
    assert(g != NULL);
    assert(b != NULL);
#define RETURNRGB(rr, gg, bb)                                                                      \
    {                                                                                              \
        *r = rr;                                                                                   \
        *g = gg;                                                                                   \
        *b = bb;                                                                                   \
        return;                                                                                    \
    }
    switch (os2color)
    {
        case CLR_FALSE:
            FIXME("this should reverse on a printer");
            RETURNRGB(0x00, 0x00, 0x00);
        case CLR_TRUE:
            FIXME("this should reverse on a printer");
            RETURNRGB(0xFF, 0xFF, 0xFF);
        case CLR_DEFAULT:
            FIXME("Let users specify this on command line or system-wide .ini?");
            RETURNRGB(0x00, 0x00, 0x00);
        case CLR_WHITE:
            RETURNRGB(0xFF, 0xFF, 0xFF);
        case CLR_BLACK:
            RETURNRGB(0x00, 0x00, 0x00);
        case CLR_BACKGROUND:
            FIXME("Let users specify this on command line or system-wide .ini?");
            RETURNRGB(0xFF, 0xFF, 0xFF);
        case CLR_BLUE:
            RETURNRGB(0x00, 0x00, 0xFF);
        case CLR_RED:
            RETURNRGB(0xFF, 0x00, 0x00);
        case CLR_PINK:
            RETURNRGB(0xFF, 0x00, 0xFF);
        case CLR_GREEN:
            RETURNRGB(0x00, 0xFF, 0x00);
        case CLR_CYAN:
            RETURNRGB(0x00, 0xFF, 0xFF);
        case CLR_YELLOW:
            RETURNRGB(0xFF, 0xFF, 0x00);
        case CLR_NEUTRAL:
            FIXME("Let users specify this on command line or system-wide .ini?");
            RETURNRGB(0x00, 0x00, 0x00);
        case CLR_DARKGRAY:
            RETURNRGB(0x80, 0x80, 0x80);
        case CLR_DARKBLUE:
            RETURNRGB(0x00, 0x00, 0x80);
        case CLR_DARKRED:
            RETURNRGB(0x80, 0x00, 0x00);
        case CLR_DARKPINK:
            RETURNRGB(0x80, 0x00, 0x80);
        case CLR_DARKGREEN:
            RETURNRGB(0x00, 0x80, 0x00);
        case CLR_DARKCYAN:
            RETURNRGB(0x00, 0x80, 0x80);
        case CLR_BROWN:
            RETURNRGB(0x80, 0x80, 0x00);
        case CLR_PALEGRAY:
            RETURNRGB(0xCC, 0xCC, 0xCC);
        default:
            break;
    }

    FIXME("unknown color index");
    RETURNRGB(0x00, 0x00, 0x00);
#undef RETURNRGB
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
    if (!ps || !rect || cx < 0 || cy < 0 || (flags & ~0x11bu) || (flags & 7) > 3)
        return FALSE;
    int64_t dx = cx, dy = cy;
    if (flags & 0x100)
    {
        dx *= WinQuerySysValue(HWND_DESKTOP, 26);
        dy *= WinQuerySysValue(HWND_DESKTOP, 27);
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
    uint32_t *pixels = malloc((size_t)(drawing.xRight - drawing.xLeft) * 4);
    if (!pixels)
        return FALSE;
    LONG fore = lx_pm_resolve_color(handle, foreground),
         back = lx_pm_resolve_color(handle, background);
    BOOL ok = (flags & 0x10) || (fore >= 0 && back >= 0);
    LONG operation = flags & 7;
    LONG mix = operation == 1 ? 4 : operation == 2 ? 12 : operation == 3 ? ps->primitive_mix[3] : 2;
    for (LONG y = drawing.yBottom; ok && y < drawing.yTop; y++)
    {
        ok = lx_pm_raster_read(handle, drawing.xLeft, y, drawing.xRight - drawing.xLeft, 1, pixels);
        for (LONG x = drawing.xLeft; ok && x < drawing.xRight; x++)
        {
            BOOL interior = (int64_t)x >= (int64_t)rect->xLeft + dx &&
                            (int64_t)x < (int64_t)rect->xRight - dx &&
                            (int64_t)y >= (int64_t)rect->yBottom + dy &&
                            (int64_t)y < (int64_t)rect->yTop - dy;
            if (interior && !(flags & 8))
                continue;
            uint32_t source;
            BOOL ink = TRUE;
            ok = lx_pm_pattern_pixel(handle, x, y, &source, &ink);
            if (!(flags & 0x10))
                source = (uint32_t)(ink != interior ? fore : back);
            else if (interior)
            {
                LONG swapped = lx_pm_resolve_color(handle, ink ? ps->primitive_back[3]
                                                               : ps->primitive_color[3]);
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
