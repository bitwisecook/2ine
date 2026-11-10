/* Public domain. No rights reserved. Wholly AI-authored. */
/* Raster primitives share clipping, logical colors, pattern tiles and Boolean mixes. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"
#include <math.h>

enum
{
    /* Pels are 0xRRGGBB. */
    RGB_MASK = 0xffffff,
    /* Longest line drawn pel by pel, along either axis. */
    LINE_MAX_SPAN = 65536
};

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
            result = RGB_MASK;
            break;
        default:
            result = source;
            break;
    }
    return result & RGB_MASK;
}

static BOOL raster_bounds(PresentationSpace *ps, LONG *width, LONG *height)
{
    PmBitmap *bitmap = ps->bitmap_lease.resource;
    *width = bitmap ? (LONG)bitmap->width : ps->window ? ps->window->w : 0;
    *height = bitmap ? (LONG)bitmap->height : ps->window ? ps->window->h : 0;
    return *width > 0 && *height > 0;
}

uint32_t lx_pm_rop3(uint32_t pattern, uint32_t source, uint32_t destination, unsigned rop)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 8; i++)
        if (rop & (1u << i))
            value |= (i & 4 ? pattern : ~pattern) & (i & 2 ? source : ~source) &
                     (i & 1 ? destination : ~destination);
    return value & RGB_MASK;
}

BOOL lx_pm_blit_rect(HPS handle, const RECTL *rect, unsigned read, LxPmBlitPel pel, void *context)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    int64_t w = (int64_t)rect->xRight - rect->xLeft, h = (int64_t)rect->yTop - rect->yBottom;
    if (!ps || w <= 0 || h <= 0 || w > LX_PM_RASTER_MAX_DIMENSION || h > LX_PM_RASTER_MAX_DIMENSION)
        return FALSE;
    uint32_t *pels = calloc((size_t)w * h, sizeof *pels);
    if (!pels)
        return FALSE;
    BOOL ok = TRUE;
    if (read == LX_PM_BLIT_READ)
        ok = lx_pm_raster_read(handle, rect->xLeft, rect->yBottom, (unsigned)w, (unsigned)h, pels);
    LONG target_width, target_height;
    if (read == LX_PM_BLIT_READ_VISIBLE && raster_bounds(ps, &target_width, &target_height))
    {
        LONG left = rect->xLeft > 0 ? rect->xLeft : 0;
        LONG bottom = rect->yBottom > 0 ? rect->yBottom : 0;
        LONG right = rect->xRight < target_width ? rect->xRight : target_width;
        LONG top = rect->yTop < target_height ? rect->yTop : target_height;
        if (left < right && bottom < top)
        {
            size_t width = (size_t)(right - left), height = (size_t)(top - bottom);
            uint32_t *visible = malloc(width * height * sizeof *visible);
            ok = visible && lx_pm_raster_read(handle, left, bottom, (unsigned)width,
                                              (unsigned)height, visible);
            if (ok)
                for (size_t row = 0; row < height; row++)
                    memcpy(pels + ((size_t)((int64_t)bottom - rect->yBottom) + row) * w +
                               ((int64_t)left - rect->xLeft),
                           visible + row * width, width * sizeof *visible);
            free(visible);
        }
    }
    for (LONG y = 0; ok && y < h; y++)
        for (LONG x = 0; ok && x < w; x++)
            ok = pel(context, rect->xLeft + x, rect->yBottom + y, &pels[(size_t)y * w + x]) !=
                 LX_PM_PEL_FAIL;
    if (ok)
        ok = lx_pm_raster_write(handle, rect->xLeft, rect->yBottom, (unsigned)w, (unsigned)h, pels);
    free(pels);
    return ok;
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
    LONG color = lx_pm_resolve_color(handle, *foreground ? ps->primitive_color[PRIM_AREA - 1]
                                                         : ps->primitive_back[PRIM_AREA - 1]);
    if (color < 0)
        return FALSE;
    *rgb = (uint32_t)color;
    return TRUE;
}

BOOL lx_pm_fill_span(HPS handle, LONG x, LONG y, LONG count, unsigned primitive)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    LONG width, height;
    if (!ps || primitive > PRIM_IMAGE - 1 || count < 0 || !raster_bounds(ps, &width, &height))
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
        if (primitive == PRIM_AREA - 1)
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
    pixel = lx_pm_mix_pixel(line->rgb, pixel, line->ps->primitive_mix[PRIM_LINE - 1]);
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
    if (ps->line_type == LINETYPE_INVISIBLE || ps->primitive_mix[PRIM_LINE - 1] == FM_LEAVEALONE)
        return lx_pm_hits(ps);
    LONG rgb = lx_pm_resolve_color(handle, ps->primitive_color[PRIM_LINE - 1]);
    if (rgb < 0)
        return 0;
    if (ps->window && ps->window->heavy.window && !ps->clip_lease.resource &&
        ps->primitive_mix[PRIM_LINE - 1] == FM_OVERPAINT && ps->line_type == LINETYPE_SOLID)
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
    if (llabs((int64_t)point->x - from.x) > LINE_MAX_SPAN ||
        llabs((int64_t)point->y - from.y) > LINE_MAX_SPAN)
        return 0;
    LinePels line = {handle, ps, width, height, (uint32_t)rgb};
    return lx_pm_walk_line(from.x, from.y, point->x, point->y, TRUE, line_pel, &line)
               ? lx_pm_hits(ps)
               : 0;
}
