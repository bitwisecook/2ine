/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Presentation spaces: the one table that WinGetPS, WinBeginPaint, GpiCreatePS and every
   engine entry look handles up in, their default attributes, and their release. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"

PresentationSpace *getPresentationSpaceFromHPS(const AnchorBlock *anchor, const HPS hps)
{
    if (hps >= FIRST_HPS_VALUE && !(hps & 1))
    {
        const size_t idx = (size_t)(hps - FIRST_HPS_VALUE) / 2;
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
    while (ps->paths)
    {
        PmPath *path = ps->paths;
        ps->paths = path->next;
        free(path->points);
        free(path);
    }
    lx_handle_release(&ps->palette_lease);
    while (ps->saved)
    {
        PmSavedSpace *saved = ps->saved;
        ps->saved = saved->next;
        lx_handle_release(&saved->palette_lease);
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
    if (paint)
        TRACE_EVENT(
            "PAINT hwnd=%u parent=%u class=%s style=%x classStyle=%x size=%d,%d invalid=%d,%d,%d,%d",
            win->hwnd, win->parent ? win->parent->hwnd : 0, win->window_class_name, win->style,
            win->class_style, win->w, win->h, win->invalid.xLeft, win->invalid.yBottom,
            win->invalid.xRight, win->invalid.yTop);

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
        /* Lotus masks bit zero from a screen PS before releasing it. Keep
           handles even so this cannot select a different, live PS. */
        hps = (HPS)(((size_t)(ps - anchor->pres_spaces)) * 2 + FIRST_HPS_VALUE);
    }

    ps->hps = hps;
    ps->window = win;
    ps->dc = hwnd;
    if (fresh)
    {
        ps->codepage = 437;
        ps->color = CLR_BLACK;
        ps->back_color = CLR_WHITE;
        ps->mix = FM_OVERPAINT;
        ps->back_mix = BM_LEAVEALONE;
        for (unsigned i = 0; i < 5; i++)
        {
            ps->primitive_color[i] = CLR_BLACK;
            ps->primitive_back[i] = CLR_WHITE;
            ps->primitive_mix[i] = FM_OVERPAINT;
            ps->primitive_back_mix[i] = BM_LEAVEALONE;
        }
        ps->line_type = LINETYPE_SOLID;
        ps->pattern = PATSYM_SOLID;
        ps->viewport = (RECTL){0, 0, win->w, win->h};
    }

    if (paint)
    {
        /* Capture before validation. An empty update region must stay empty,
           including when an app sends WM_PAINT itself between timer ticks. */
        ps->paint_clip = win->invalid;
        ps->paint_clip_set = TRUE;
        if (prclPaint)
            *prclPaint = ps->paint_clip;
        validateWindowPaint(anchor, win);
    }
    return hps;
}

/* The presentation space of a handle on the calling thread, which owns its anchor block. */
PresentationSpace *lx_pm_font_space(HPS handle)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    return anchor ? getPresentationSpaceFromHPS(anchor, handle) : NULL;
}

BOOL lx_pm_valid_ps(HPS hps)
{
    return lx_pm_font_space(hps) != NULL;
}

HPS lx_pm_create_ps(HAB hab, HDC dc)
{
    if (!getAnchorBlock(hab))
        return 0;
    if (dc && !lx_pm_dc_valid(dc))
        return 0;
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    HPS result =
        acquirePresentationSpace(getWindowFromHWND(anchor, dc) ? dc : HWND_DESKTOP, 0, NULL, FALSE);
    PresentationSpace *ps = result ? getPresentationSpaceFromHPS(anchor, result) : NULL;
    if (ps && lx_pm_associate_dc(result, dc))
        ps->persistent = 1;
    else if (ps)
    {
        destroyPresentationSpace(anchor, ps);
        result = 0;
    }
    return result;
}

BOOL lx_pm_destroy_ps(HPS hps)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, hps) : NULL;
    if (!ps)
        return FALSE;
    destroyPresentationSpace(anchor, ps);
    return TRUE;
}

HPS WinGetPS(HWND window)
{
    return acquirePresentationSpace(window, 0, NULL, FALSE);
}

HPS lx_pm_get_clip_ps(HWND window, ULONG flags)
{
    HPS handle = WinGetPS(window);
    PresentationSpace *ps = handle ? lx_pm_font_space(handle) : NULL;
    if (ps)
        ps->clip_flags = flags;
    return handle;
}

HPS WinGetScreenPS(HWND desktop)
{
    if (desktop != HWND_DESKTOP)
        return 0;
    return WinGetPS(desktop);
}

BOOL WinReleasePS(HPS space)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    PresentationSpace *ps = anchor ? getPresentationSpaceFromHPS(anchor, space) : NULL;
    if (!ps || ps->persistent)
        return FALSE;
    if (ps->window && ps->window->heavy.window)
        presentWindow(anchor, ps->window);
    destroyPresentationSpace(anchor, ps);
    return TRUE;
}

BOOL lx_pm_record_point(PresentationSpace *ps, const POINTL *point, BOOL move)
{
    PmPath *path = ps->record_path;
    if (!path)
        return TRUE;
    if (path->count == path->capacity)
    {
        size_t capacity = path->capacity ? path->capacity * 2 : 64;
        if (capacity > 65536)
            return FALSE;
        PmPathPoint *points = realloc(path->points, capacity * sizeof *points);
        if (!points)
            return FALSE;
        path->points = points;
        path->capacity = capacity;
    }
    POINTL device = *point;
    if (!lx_pm_transform_point(ps, &device, TRUE))
        return FALSE;
    path->points[path->count++] = (PmPathPoint){*point, move, device};
    return TRUE;
}

BOOL lx_pm_move(HPS hps, const POINTL *point)
{
    PresentationSpace *ps = lx_pm_font_space(hps);
    if (!ps || !point)
        return FALSE;
    if (!lx_pm_record_point(ps, point, TRUE))
        return FALSE;
    ps->position = *point;
    return TRUE;
}

BOOL lx_pm_erase(HPS hps)
{
    PresentationSpace *ps = lx_pm_font_space(hps);
    if (!ps || (!ps->window && !ps->bitmap_lease.resource))
        return FALSE;
    PmBitmap *bitmap = ps->bitmap_lease.resource;
    RECTL rect = {0, 0, bitmap ? (LONG)bitmap->width : ps->window->w,
                  bitmap ? (LONG)bitmap->height : ps->window->h};
    return WinFillRect(hps, &rect, CLR_BACKGROUND);
}
