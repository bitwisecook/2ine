/* Public domain. No rights reserved. Wholly AI-authored. */
/* Raster primitives share clipping, logical colors, pattern tiles and Boolean mixes. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmgre/graphics.h"
#include "os2/include/private/pmgrep.h"
#include <math.h>

uint32_t lx_pm_mix_pixel(uint32_t source, uint32_t destination, LONG mix)
{
    uint32_t result;
    switch (mix)
    {
        case FM_OR:
            result = source | destination;
            break;
        case FM_OVERPAINT:
            result = source;
            break;
        case FM_XOR:
            result = source ^ destination;
            break;
        case FM_LEAVEALONE:
            result = destination;
            break;
        case FM_AND:
            result = source & destination;
            break;
        case FM_SUBTRACT:
            result = ~source & destination;
            break;
        case FM_MASKSRCNOT:
            result = source & ~destination;
            break;
        case FM_ZERO:
            result = 0;
            break;
        case FM_NOTMERGESRC:
            result = ~(source | destination);
            break;
        case FM_NOTXORSRC:
            result = ~(source ^ destination);
            break;
        case FM_INVERT:
            result = ~destination;
            break;
        case FM_MERGESRCNOT:
            result = source | ~destination;
            break;
        case FM_NOTCOPYSRC:
            result = ~source;
            break;
        case FM_MERGENOTSRC:
            result = ~source | destination;
            break;
        case FM_NOTMASKSRC:
            result = ~(source & destination);
            break;
        case FM_ONE:
            result = 0xffffff;
            break;
        default:
            result = source;
            break;
    }
    return result & 0xffffff;
}

static BOOL raster_bounds(PresentationSpace *ps, LONG *width, LONG *height)
{
    PmBitmap *bitmap = ps->bitmap_lease.resource;
    *width = bitmap ? (LONG)bitmap->width : ps->window ? ps->window->w : 0;
    *height = bitmap ? (LONG)bitmap->height : ps->window ? ps->window->h : 0;
    return *width > 0 && *height > 0;
}

BOOL lx_pm_pattern_pixel(HPS handle, LONG x, LONG y, uint32_t *rgb, BOOL *foreground)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !rgb || !foreground)
        return FALSE;
    *foreground = TRUE;
    if (ps->pattern_set)
    {
        POINTL reference = ps->pattern_reference;
        if (!lx_pm_transform_point(ps, &reference, TRUE))
            return FALSE;
        PmBitmap *tile = ps->bitmap_ids[ps->pattern_set].resource;
        if (!tile)
            return FALSE;
        LONG tx = ((int64_t)x - reference.x) % tile->width,
             ty = ((int64_t)y - reference.y) % tile->height;
        if (tx < 0)
            tx += tile->width;
        if (ty < 0)
            ty += tile->height;
        *rgb = tile->pixels[(size_t)ty * tile->width + tx];
        if (tile->depth != 1)
            return TRUE;
        *foreground = *rgb != 0;
    }
    else if (ps->pattern == PATSYM_NOSHADE || ps->pattern == PATSYM_BLANK)
        *foreground = FALSE;
    else if (ps->pattern != PATSYM_SOLID)
        return FALSE;
    LONG color =
        lx_pm_resolve_color(handle, *foreground ? ps->primitive_color[3] : ps->primitive_back[3]);
    if (color < 0)
        return FALSE;
    *rgb = (uint32_t)color;
    return TRUE;
}

BOOL lx_pm_fill_span(HPS handle, LONG x, LONG y, LONG count, unsigned primitive)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    LONG width, height;
    if (!ps || primitive >= 5 || count < 0 || !raster_bounds(ps, &width, &height))
        return FALSE;
    int64_t end = (int64_t)x + count;
    LONG left = x < 0 ? 0 : x, right = end > width ? width : (LONG)end;
    if (y < 0 || y >= height || right <= left)
        return TRUE;
    uint32_t *pixels = malloc((size_t)(right - left) * sizeof *pixels);
    if (!pixels)
        return FALSE;
    BOOL ok = lx_pm_raster_read(handle, left, y, right - left, 1, pixels);
    LONG solid = lx_pm_resolve_color(handle, ps->primitive_color[primitive]);
    for (LONG column = left; ok && column < right; column++)
    {
        BOOL foreground = TRUE;
        uint32_t source = (uint32_t)solid;
        if (primitive == 3)
            ok = lx_pm_pattern_pixel(handle, column, y, &source, &foreground);
        else if (solid < 0)
            ok = FALSE;
        LONG mix = foreground ? ps->primitive_mix[primitive] : ps->primitive_back_mix[primitive];
        if (ok)
            pixels[column - left] = lx_pm_mix_pixel(source, pixels[column - left], mix);
    }
    if (ok)
        ok = lx_pm_raster_write(handle, left, y, right - left, 1, pixels);
    free(pixels);
    return ok;
}

typedef struct LinePels
{
    HPS handle;
    PresentationSpace *ps;
    LONG width, height;
    uint32_t rgb;
} LinePels;

static BOOL line_pel(void *context, LONG x, LONG y)
{
    LinePels *line = context;
    if (x < 0 || y < 0 || x >= line->width || y >= line->height)
        return TRUE;
    uint32_t pixel;
    if (!lx_pm_raster_read(line->handle, x, y, 1, 1, &pixel))
        return FALSE;
    pixel = lx_pm_mix_pixel(line->rgb, pixel, line->ps->primitive_mix[0]);
    return lx_pm_raster_write(line->handle, x, y, 1, 1, &pixel);
}

LONG lx_pm_line(HPS handle, const POINTL *point, int pel)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    LONG width, height;
    if (ps && point && ps->record_path && !pel)
    {
        if (!lx_pm_record_point(ps, point, FALSE))
            return 0;
        ps->position = *point;
        return 1;
    }
    if (!ps || !point || !raster_bounds(ps, &width, &height))
        return 0;
    POINTL from = pel ? *point : ps->position;
    if (!pel)
        ps->position = *point;
    POINTL end = *point;
    if (!lx_pm_transform_point(ps, &from, TRUE) || !lx_pm_transform_point(ps, &end, TRUE))
        return 0;
    point = &end;
    BOOL hit = ps->gpi.pick_hit;
    lx_pm_boundary(ps, from.x < end.x ? from.x : end.x, from.y < end.y ? from.y : end.y,
                   (LONG)llabs((int64_t)end.x - from.x) + 1,
                   (LONG)llabs((int64_t)end.y - from.y) + 1);
    ps->gpi.pick_hit = hit; /* The segment itself, not its bounding box, decides a hit. */
    lx_pm_pick_line(ps, &from, &end);
    if (ps->gpi.initialized && !ps->gpi.display_enabled)
        return lx_pm_hits(ps);
    if (ps->line_type == LINETYPE_INVISIBLE || ps->primitive_mix[0] == FM_LEAVEALONE)
        return lx_pm_hits(ps);
    LONG rgb = lx_pm_resolve_color(handle, ps->primitive_color[0]);
    if (rgb < 0)
        return 0;
    if (ps->window && ps->window->heavy.window && !ps->clip_lease.resource &&
        ps->primitive_mix[0] == FM_OVERPAINT && ps->line_type == LINETYPE_SOLID)
    {
        LxHostRect origin;
        calcHeavyweightRect(ps->window, &origin);
        PmRegion visible = {0};
        BOOL ok = lx_pm_window_clip(handle, NULL, &visible);
        for (size_t i = 0; ok && i < visible.count; i++)
        {
            RECTL part = visible.rects[i];
            LxHostRect clip = {origin.x + part.xLeft, origin.y + origin.h - part.yTop,
                               part.xRight - part.xLeft, part.yTop - part.yBottom};
            ok = lx_host_window_line(ps->window->heavy.window, &clip, origin.x + from.x,
                                     origin.y + origin.h - 1 - from.y, origin.x + point->x,
                                     origin.y + origin.h - 1 - point->y, (rgb >> 16) & 255,
                                     (rgb >> 8) & 255, rgb & 255);
        }
        free(visible.rects);
        return ok ? lx_pm_hits(ps) : 0;
    }
    if (llabs((int64_t)point->x - from.x) > 65536 || llabs((int64_t)point->y - from.y) > 65536)
        return 0;
    LinePels line = {handle, ps, width, height, (uint32_t)rgb};
    return lx_pm_walk_line(from.x, from.y, point->x, point->y, TRUE, line_pel, &line)
               ? lx_pm_hits(ps)
               : 0;
}
