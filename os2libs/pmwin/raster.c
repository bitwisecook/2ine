/* Public domain. No rights reserved. Wholly AI-authored. */
/* Raster primitives share clipping, logical colors, pattern tiles and Boolean mixes. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"

uint32_t lx_pm_mix_pixel(uint32_t source, uint32_t destination, LONG mix)
{
    uint32_t result;
    switch (mix)
    {
        case 1:
            result = source | destination;
            break;
        case 2:
            result = source;
            break;
        case 4:
            result = source ^ destination;
            break;
        case 5:
            result = destination;
            break;
        case 6:
            result = source & destination;
            break;
        case 7:
            result = ~source & destination;
            break;
        case 8:
            result = source & ~destination;
            break;
        case 9:
            result = 0;
            break;
        case 10:
            result = ~(source | destination);
            break;
        case 11:
            result = ~(source ^ destination);
            break;
        case 12:
            result = ~destination;
            break;
        case 13:
            result = source | ~destination;
            break;
        case 14:
            result = ~source;
            break;
        case 15:
            result = ~source | destination;
            break;
        case 16:
            result = ~(source & destination);
            break;
        case 17:
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
    else if (ps->pattern == 15 || ps->pattern == 64)
        *foreground = FALSE;
    else if (ps->pattern != 16)
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
LONG lx_pm_line(HPS handle, const POINTL *point, int pel)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    LONG width, height;
    if (!ps || !point || !raster_bounds(ps, &width, &height))
        return 0;
    POINTL from = pel ? *point : ps->position;
    if (!pel)
        ps->position = *point;
    POINTL end = *point;
    if (!lx_pm_transform_point(ps, &from, TRUE) || !lx_pm_transform_point(ps, &end, TRUE))
        return 0;
    point = &end;
    if (ps->line_type == 8 || ps->primitive_mix[0] == 5)
        return 1;
    LONG rgb = lx_pm_resolve_color(handle, ps->primitive_color[0]);
    if (rgb < 0)
        return 0;
    if (ps->window && ps->window->heavy.window && !ps->clip_lease.resource &&
        ps->primitive_mix[0] == 2 && ps->line_type == 7)
    {
        LxHostRect clip;
        calcHeavyweightRect(ps->window, &clip);
        return lx_host_window_line(ps->window->heavy.window, &clip, clip.x + from.x,
                                   clip.y + clip.h - 1 - from.y, clip.x + point->x,
                                   clip.y + clip.h - 1 - point->y, (rgb >> 16) & 255,
                                   (rgb >> 8) & 255, rgb & 255)
                   ? 1
                   : 0;
    }
    int64_t dx = llabs((int64_t)point->x - from.x), dy = llabs((int64_t)point->y - from.y);
    if (dx > 65536 || dy > 65536)
        return 0;
    LONG x = from.x, y = from.y, sx = x < point->x ? 1 : -1, sy = y < point->y ? 1 : -1;
    int64_t error = dx - dy;
    BOOL ok = TRUE;
    while (ok)
    {
        if (x >= 0 && y >= 0 && x < width && y < height)
        {
            uint32_t pixel;
            ok = lx_pm_raster_read(handle, x, y, 1, 1, &pixel);
            if (ok)
            {
                pixel = lx_pm_mix_pixel(rgb, pixel, ps->primitive_mix[0]);
                ok = lx_pm_raster_write(handle, x, y, 1, 1, &pixel);
            }
        }
        if (x == point->x && y == point->y)
            break;
        int64_t twice = error * 2;
        if (twice > -dy)
        {
            error -= dy;
            x += sx;
        }
        if (twice < dx)
        {
            error += dx;
            y += sy;
        }
    }
    return ok ? 1 : 0;
}
