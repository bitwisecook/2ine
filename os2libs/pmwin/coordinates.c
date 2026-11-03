/* Public domain. No rights reserved. Wholly AI-authored. */
/* Page units and an explicit viewport map model/view coordinates to device pels. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
static void unit_scale(PresentationSpace *ps, LONG *pels, LONG *units)
{
    *pels = (LONG)lx_pm_dc_dpi(ps->dc);
    switch (ps->page_units)
    {
        case 0x0c:
            *units = 254;
            break; /* 0.1 millimetre */
        case 0x10:
            *units = 2540;
            break; /* 0.01 millimetre */
        case 0x14:
            *units = 100;
            break; /* 0.01 inch */
        case 0x18:
            *units = 1000;
            break; /* 0.001 inch */
        case 0x1c:
            *units = 1440;
            break; /* twips */
        default:
            *pels = *units = 1;
            break;
    }
}
SIZEL lx_pm_page_dimensions(PresentationSpace *ps)
{
    PmBitmap *bitmap = ps->bitmap_lease.resource;
    LONG width = ps->window ? ps->window->w : bitmap ? (LONG)bitmap->width : 0;
    LONG height = ps->window ? ps->window->h : bitmap ? (LONG)bitmap->height : 0;
    LONG pels, units;
    unit_scale(ps, &pels, &units);
    return (SIZEL){
        ps->page_size.cx ? ps->page_size.cx : (LONG)(((int64_t)width * units + pels / 2) / pels),
        ps->page_size.cy ? ps->page_size.cy : (LONG)(((int64_t)height * units + pels / 2) / pels)};
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
    {
        LONG pels, units;
        unit_scale(ps, &pels, &units);
        POINTL output;
        if (!convert_coordinate(point->x, 0, pels, units, to_device, &output.x) ||
            !convert_coordinate(point->y, 0, pels, units, to_device, &output.y))
            return FALSE;
        *point = output;
        return TRUE;
    }
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
    /* Model and viewing transforms are identity; page units still scale device pels. */
    if ((source == 5) == (target == 5) || !count)
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
