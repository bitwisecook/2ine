/* Public domain. No rights reserved. Wholly AI-authored. */
/* Pixel-unit model/view coordinates map through the PS page viewport to device pels. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
SIZEL lx_pm_page_dimensions(PresentationSpace *ps)
{
    PmBitmap *bitmap = ps->bitmap_lease.resource;
    LONG width = ps->window ? ps->window->w : bitmap ? (LONG)bitmap->width : 0;
    LONG height = ps->window ? ps->window->h : bitmap ? (LONG)bitmap->height : 0;
    return (SIZEL){ps->page_size.cx ? ps->page_size.cx : width,
                   ps->page_size.cy ? ps->page_size.cy : height};
}
static BOOL convert_coordinate(LONG value, LONG origin, LONG span, LONG page, BOOL to_device,
                               LONG *output)
{
    if (span <= 0 || page <= 0)
        return FALSE;
    int64_t product = to_device ? (int64_t)value * span : ((int64_t)value - origin) * page;
    LONG divisor = to_device ? page : span;
    int64_t converted = (product + (product < 0 ? -(int64_t)divisor / 2 : divisor / 2)) / divisor;
    if (to_device)
        converted += origin;
    if (converted < INT32_MIN || converted > INT32_MAX)
        return FALSE;
    *output = (LONG)converted;
    return TRUE;
}
BOOL lx_pm_transform_point(PresentationSpace *ps, POINTL *point, BOOL to_device)
{
    if (!ps || !point)
        return FALSE;
    if (!ps->viewport_set)
        return TRUE;
    SIZEL page = lx_pm_page_dimensions(ps);
    RECTL view = ps->viewport;
    POINTL output;
    if (!convert_coordinate(point->x, view.xLeft, view.xRight - view.xLeft, page.cx, to_device,
                            &output.x) ||
        !convert_coordinate(point->y, view.yBottom, view.yTop - view.yBottom, page.cy, to_device,
                            &output.y))
        return FALSE;
    *point = output;
    return TRUE;
}
BOOL lx_pm_convert_points(HPS handle, LONG source, LONG target, LONG count, PPOINTL points)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || source < 1 || source > 5 || target < 1 || target > 5 || count < 0 || count > 65536 ||
        (count && !points))
        return FALSE;
    /* Model and viewing transforms are identity in the admitted pixel-unit profile. */
    if ((source == 5) == (target == 5) || !ps->viewport_set || !count)
        return TRUE;
    POINTL *converted = malloc((size_t)count * sizeof *converted);
    if (!converted)
        return FALSE;
    BOOL ok = TRUE;
    for (LONG i = 0; i < count && ok; i++)
    {
        converted[i] = points[i];
        ok = lx_pm_transform_point(ps, &converted[i], target == 5);
    }
    if (ok)
        memcpy(points, converted, (size_t)count * sizeof *points);
    free(converted);
    return ok;
}
