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
    return lx_pm_rect_visible(ps, rect);
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
