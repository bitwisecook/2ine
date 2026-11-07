/* Public domain. No rights reserved. Wholly AI-authored. */
/* GPI region entrypoints share normalized geometry with PM invalidation and drawing. */
#include "os2libs/pmgpi.h"
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
ULONG GpiCreateRegion(HPS ps, LONG count, PRECTL rects)
{
    return lx_pm_region_create(ps, count, rects);
}
BOOL GpiDestroyRegion(HPS ps, ULONG region)
{
    return lx_pm_region_destroy(ps, region);
}
LONG GpiCombineRegion(HPS ps, ULONG destination, ULONG first, ULONG second, LONG mode)
{
    return lx_pm_region_combine(ps, destination, first, second, mode);
}
BOOL GpiSetRegion(HPS ps, ULONG region, LONG count, PRECTL rects)
{
    return lx_pm_region_set(ps, region, count, rects) != 0;
}
LONG GpiQueryRegionBox(HPS ps, ULONG region, PRECTL rect)
{
    return lx_pm_region_box(ps, region, rect);
}
LONG GpiSetClipRegion(HPS ps, ULONG region, PULONG old)
{
    return lx_pm_set_clip(ps, region, old);
}
ULONG GpiQueryClipRegion(HPS handle)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    PmRegion *r = ps ? ps->clip_lease.resource : NULL;
    return r ? r->handle : 0;
}
LONG GpiOffsetRegion(HPS ps, ULONG region, PPOINTL point)
{
    return lx_pm_region_offset(ps, region, point);
}
LONG GpiOffsetClipRegion(HPS ps, PPOINTL point)
{
    ULONG region = GpiQueryClipRegion(ps);
    return region ? lx_pm_region_offset(ps, region, point) : lx_pm_valid_ps(ps) ? 2 : 0;
}
LONG GpiRectVisible(HPS ps, PRECTL rect)
{
    LONG result = lx_pm_rect_visible(ps, rect);
    TRACE_NATIVE("GpiRectVisible(%u, %d,%d,%d,%d) -> %d", (uint)ps, rect ? (int)rect->xLeft : 0,
                 rect ? (int)rect->yBottom : 0, rect ? (int)rect->xRight : 0,
                 rect ? (int)rect->yTop : 0, (int)result);
    return result;
}
LONG GpiQueryClipBox(HPS handle, PRECTL rect)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !rect)
        return 0;
    PmRegion *r = ps->clip_lease.resource;
    if (r)
        return lx_pm_region_box(handle, r->handle, rect);
    *rect = ps->window ? (RECTL){0, 0, ps->window->w, ps->window->h} : ps->viewport;
    if (ps->bitmap_lease.resource)
    {
        PmBitmap *b = ps->bitmap_lease.resource;
        *rect = (RECTL){0, 0, (LONG)b->width, (LONG)b->height};
    }
    return WinIsRectEmpty(0, rect) ? 1 : 2;
}
LONG GpiIntersectClipRectangle(HPS handle, PRECTL rectangle)
{
    if (!rectangle || !lx_pm_valid_ps(handle))
        return 0;
    ULONG first = GpiQueryClipRegion(handle), created = 0;
    if (!first)
    {
        RECTL rect;
        if (!GpiQueryClipBox(handle, &rect))
            return 0;
        first = created = GpiCreateRegion(handle, 1, &rect);
    }
    ULONG second = GpiCreateRegion(handle, 1, rectangle),
          destination = GpiCreateRegion(handle, 0, NULL);
    LONG result = 0;
    if (first && second && destination && GpiCombineRegion(handle, destination, first, second, 6))
        result = GpiSetClipRegion(handle, destination, NULL);
    if (!result && destination)
        GpiDestroyRegion(handle, destination);
    if (created)
        GpiDestroyRegion(handle, created);
    if (second)
        GpiDestroyRegion(handle, second);
    return result;
}

BOOL GpiQueryRegionRects(HPS ps, ULONG handle, PRECTL bounds, RGNRECT *control, PRECTL rects)
{
    return lx_pm_region_query_rects(ps, handle, bounds, control, rects);
}

LONG GpiPtInRegion(HPS ps, ULONG handle, PPOINTL point)
{
    LxHandleBorrow borrow = {0};
    if (!lx_pm_valid_ps(ps) || !point || !lx_handle_borrow(handle, LX_HANDLE_PM_REGION, &borrow))
        return 0;
    PmRegion *r = borrow.resource;
    LONG result = 1;
    if (r->anchor != lxHostThreadState()->anchor_block || r->selected)
        result = 0;
    else
        for (size_t i = 0; i < r->count; i++)
        {
            RECTL q = r->rects[i];
            if (point->x >= q.xLeft && point->x < q.xRight && point->y >= q.yBottom &&
                point->y < q.yTop)
            {
                result = 2;
                break;
            }
        }
    lx_handle_release(&borrow);
    return result;
}
LONG GpiRectInRegion(HPS ps, ULONG handle, PRECTL rectangle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_pm_valid_ps(ps) || !rectangle || rectangle->xRight < rectangle->xLeft ||
        rectangle->yTop < rectangle->yBottom ||
        !lx_handle_borrow(handle, LX_HANDLE_PM_REGION, &borrow))
        return 0;
    PmRegion *r = borrow.resource;
    LONG result = 1;
    if (r->anchor != lxHostThreadState()->anchor_block || r->selected)
        result = 0;
    else
    {
        PmRegion input = {.rects = rectangle, .count = 1}, remaining = {0};
        if (!regionBoolean(&remaining, &input, r, 7))
            result = 0;
        else if (!remaining.count)
            result = 3;
        else
        {
            PmRegion overlap = {0};
            if (!regionBoolean(&overlap, &input, r, 6))
                result = 0;
            else if (overlap.count)
                result = 2;
            free(overlap.rects);
        }
        free(remaining.rects);
    }
    lx_handle_release(&borrow);
    return result;
}
LONG GpiPaintRegion(HPS ps, ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_pm_valid_ps(ps) || !lx_handle_borrow(handle, LX_HANDLE_PM_REGION, &borrow))
        return 0;
    PmRegion *r = borrow.resource;
    BOOL ok = r->anchor == lxHostThreadState()->anchor_block && !r->selected;
    for (size_t i = 0; ok && i < r->count; i++)
    {
        RECTL q = r->rects[i];
        if ((int64_t)q.yTop - q.yBottom > 65536 || (int64_t)q.xRight - q.xLeft > 65536)
        {
            ok = FALSE;
            break;
        }
        for (LONG y = q.yBottom; ok && y < q.yTop; y++)
            ok = lx_pm_fill_span(ps, q.xLeft, y, q.xRight - q.xLeft, 3);
    }
    lx_handle_release(&borrow);
    return ok ? lx_pm_hits(lx_pm_font_space(ps)) : 0;
}
