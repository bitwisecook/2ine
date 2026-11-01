/* Public domain. No rights reserved. Wholly AI-authored. */
/* Pens retain LOGPEN metadata; strokes apply one Boolean mix per covered pixel. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/graphics.h"
#include <math.h>
typedef struct Open32Pen
{
    LONG style, width;
    ULONG color;
    BOOL stock;
} Open32Pen;
ULONG CreatePen(LONG style, LONG width, ULONG color)
{
    if (style < 0 || style > 6 || width < 0 || width > 512 || (color & 0xFF000000u))
    {
        open32SetError(87);
        return 0;
    }
    Open32Pen *pen = calloc(1, sizeof *pen);
    if (!pen)
    {
        open32SetError(8);
        return 0;
    }
    pen->style = width > 1 && style >= 1 && style <= 4 ? 0 : style;
    pen->width = width;
    pen->color = color;
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_PEN, pen, free);
    if (!handle)
    {
        free(pen);
        open32SetError(8);
    }
    return handle;
}
ULONG CreatePenIndirect(PVOID record)
{
    if (!record)
    {
        open32SetError(87);
        return 0;
    }
    return CreatePen((LONG)lx_guest_load_u32(record), (LONG)lx_guest_load_u32((uint8 *)record + 4),
                     lx_guest_load_u32((uint8 *)record + 12));
}
ULONG open32StockPen(LONG index)
{
    static _Thread_local ULONG stocks[3];
    if (index < 6 || index > 8)
        return 0;
    unsigned slot = (unsigned)index - 6;
    if (!stocks[slot])
    {
        ULONG handle = CreatePen(index == 8 ? 5 : 0, 0, index == 6 ? 0xFFFFFF : 0);
        LxHandleBorrow borrow = {0};
        if (!handle || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_PEN, &borrow))
            return 0;
        ((Open32Pen *)borrow.resource)->stock = TRUE;
        lx_handle_release(&borrow);
        stocks[slot] = handle;
    }
    return stocks[slot];
}
ULONG open32SelectPen(Open32DC *dc, ULONG handle)
{
    LxHandleBorrow next = {0};
    if (!dc || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_PEN, &next))
        return 0;
    ULONG previous = dc->pen_handle;
    lx_handle_release(&dc->pen);
    dc->pen = next;
    dc->pen_handle = handle;
    return previous;
}
BOOL open32DeletePen(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_PEN, &borrow))
        return FALSE;
    BOOL result = !((Open32Pen *)borrow.resource)->stock &&
                  lx_handle_close_guarded(handle, LX_HANDLE_OPEN32_PEN, 1, NULL) == 1;
    lx_handle_release(&borrow);
    return result;
}
LONG open32PenObject(ULONG handle, LONG capacity, PVOID output)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_PEN, &borrow))
        return 0;
    Open32Pen *pen = borrow.resource;
    LONG result = !output || capacity >= 16 ? 16 : 0;
    if (output && result)
    {
        lx_guest_store_u32(output, (ULONG)pen->style);
        lx_guest_store_u32((uint8 *)output + 4, (ULONG)pen->width);
        lx_guest_store_u32((uint8 *)output + 8, 0);
        lx_guest_store_u32((uint8 *)output + 12, pen->color);
    }
    lx_handle_release(&borrow);
    return result;
}
BOOL MoveToEx(ULONG handle, LONG x, LONG y, PVOID previous)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc)
    {
        open32SetError(6);
        return FALSE;
    }
    if (previous)
    {
        lx_guest_store_u32(previous, (ULONG)dc->position_x);
        lx_guest_store_u32((uint8 *)previous + 4, (ULONG)dc->position_y);
    }
    dc->position_x = x;
    dc->position_y = y;
    return TRUE;
}
LONG SetROP2(ULONG handle, LONG mode)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || mode < 1 || mode > 16)
    {
        open32SetError(dc ? 87 : 6);
        return 0;
    }
    LONG previous = dc->rop2;
    dc->rop2 = mode;
    return previous;
}
static BOOL dash_pixel(LONG style, uint64_t step)
{
    static const unsigned lengths[4][6] = {{6, 2}, {1, 2}, {6, 2, 1, 2}, {6, 2, 1, 2, 1, 2}};
    static const unsigned counts[] = {2, 2, 4, 6}, cycles[] = {8, 3, 11, 14};
    if (style < 1 || style > 4)
        return TRUE;
    unsigned part = (unsigned)(step % cycles[style - 1]);
    for (unsigned i = 0; i < counts[style - 1]; i++)
    {
        if (part < lengths[style - 1][i])
            return !(i & 1);
        part -= lengths[style - 1][i];
    }
    return TRUE;
}
BOOL LineTo(ULONG handle, LONG target_x, LONG target_y)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !open32CanDraw(handle) || !dc->pen.resource)
    {
        open32SetError(6);
        return FALSE;
    }
    Open32Pen *pen = dc->pen.resource;
    LONG x = dc->position_x, y = dc->position_y;
    int64_t dx = llabs((int64_t)target_x - x), dy = llabs((int64_t)target_y - y);
    if (dx > 65536 || dy > 65536)
    {
        open32SetError(87);
        return FALSE;
    }
    if (pen->style == 5 || (!dx && !dy))
    {
        dc->position_x = target_x;
        dc->position_y = target_y;
        return TRUE;
    }
    LONG diameter = pen->width ? pen->width : 1, before = (diameter - 1) / 2, after = diameter / 2;
    int64_t left = (int64_t)(x < target_x ? x : target_x) - before;
    int64_t top = (int64_t)(y < target_y ? y : target_y) - before;
    int64_t right = (int64_t)(x > target_x ? x : target_x) + after + 1;
    int64_t bottom = (int64_t)(y > target_y ? y : target_y) + after + 1;
    LONG canvas_width = open32DCWidth(handle), canvas_height = open32DCHeight(handle);
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right > canvas_width)
        right = canvas_width;
    if (bottom > canvas_height)
        bottom = canvas_height;
    if (right <= left || bottom <= top)
    {
        dc->position_x = target_x;
        dc->position_y = target_y;
        return TRUE;
    }
    uint64_t pixel_count = (uint64_t)(right - left) * (uint64_t)(bottom - top);
    uint64_t work = (uint64_t)((dx > dy ? dx : dy) + 1) * diameter * diameter;
    if (pixel_count > 16777216 || work > 67108864)
    {
        open32SetError(8);
        return FALSE;
    }
    size_t width = (size_t)(right - left), height = (size_t)(bottom - top),
           area = (size_t)pixel_count;
    uint8 *coverage = calloc(area, 1);
    uint32 *pixels = malloc(area * 4);
    if (!coverage || !pixels)
    {
        free(coverage);
        free(pixels);
        open32SetError(8);
        return FALSE;
    }
    int64_t error = dx - dy;
    LONG sx = x < target_x ? 1 : -1, sy = y < target_y ? 1 : -1;
    uint64_t step = 0;
    /* The final point is excluded, matching Windows LineTo and joined segments. */
    while (x != target_x || y != target_y)
    {
        uint8 value = dash_pixel(pen->style, step++) ? 1 : dc->background_mode == 2 ? 2 : 0;
        if (value)
            for (LONG oy = -before; oy <= after; oy++)
                for (LONG ox = -before; ox <= after; ox++)
                {
                    int64_t px = (int64_t)x + ox, py = (int64_t)y + oy;
                    LONG cx = 2 * (ox + before) - diameter + 1,
                         cy = 2 * (oy + before) - diameter + 1;
                    if (diameter > 1 && cx * cx + cy * cy > diameter * diameter)
                        continue;
                    if (px >= left && px < right && py >= top && py < bottom)
                        coverage[(size_t)(bottom - 1 - py) * width + (size_t)(px - left)] = value;
                }
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
    BOOL result = lx_pm_raster_read(handle, (LONG)left, canvas_height - (LONG)bottom,
                                    (unsigned)width, (unsigned)height, pixels);
    static const LONG mixes[] = {0, 9, 10, 7, 14, 8, 12, 4, 16, 6, 11, 5, 15, 2, 13, 1, 17};
    if (result)
    {
        for (size_t i = 0; i < area; i++)
            if (coverage[i])
            {
                ULONG color = coverage[i] == 1 ? pen->color : dc->background_color;
                ULONG rgb = (color & 255) << 16 | (color & 0xFF00) | (color >> 16 & 255);
                pixels[i] = lx_pm_mix_pixel(rgb, pixels[i], mixes[dc->rop2]);
            }
        result = lx_pm_raster_write(handle, (LONG)left, canvas_height - (LONG)bottom,
                                    (unsigned)width, (unsigned)height, pixels);
    }
    free(pixels);
    free(coverage);
    if (result)
    {
        dc->position_x = target_x;
        dc->position_y = target_y;
    }
    else
        open32SetError(87);
    return result;
}

LONG GetROP2(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc)
    {
        open32SetError(6);
        return 0;
    }
    return dc->rop2;
}

BOOL Rectangle(ULONG handle, LONG left, LONG top, LONG right, LONG bottom)
{
    Open32DC *dc = open32FindDC(handle);
    uint8 brush[12];
    if (!dc || !open32CanDraw(handle) || !dc->pen.resource ||
        open32BrushObject(dc->brush_handle, sizeof brush, brush) != sizeof brush)
    {
        open32SetError(6);
        return FALSE;
    }
    Open32Pen *pen = dc->pen.resource;
    if (left > right)
    {
        LONG swap = left;
        left = right;
        right = swap;
    }
    if (top > bottom)
    {
        LONG swap = top;
        top = bottom;
        bottom = swap;
    }
    if (left == right || top == bottom)
        return TRUE;
    LONG diameter = pen->width ? pen->width : 1;
    LONG before = pen->style == 6 ? 0 : (diameter - 1) / 2;
    LONG after = pen->style == 6 ? 0 : diameter / 2;
    int64_t l = (int64_t)left - before, t = (int64_t)top - before;
    int64_t r = (int64_t)right + after, b = (int64_t)bottom + after;
    LONG canvas_height = open32DCHeight(handle), canvas_width = open32DCWidth(handle);
    if (l < 0)
        l = 0;
    if (t < 0)
        t = 0;
    if (r > canvas_width)
        r = canvas_width;
    if (b > canvas_height)
        b = canvas_height;
    if (r <= l || b <= t)
        return TRUE;
    uint64_t area = (uint64_t)(r - l) * (uint64_t)(b - t);
    if (area > 16777216)
    {
        open32SetError(8);
        return FALSE;
    }
    unsigned width = (unsigned)(r - l), height = (unsigned)(b - t);
    uint32 *pixels = malloc((size_t)area * 4);
    if (!pixels)
    {
        open32SetError(8);
        return FALSE;
    }
    BOOL result =
        lx_pm_raster_read(handle, (LONG)l, canvas_height - (LONG)b, width, height, pixels);
    static const LONG mixes[] = {0, 9, 10, 7, 14, 8, 12, 4, 16, 6, 11, 5, 15, 2, 13, 1, 17};
    if (result)
        for (unsigned row = 0; row < height; row++)
            for (unsigned column = 0; column < width; column++)
            {
                int64_t x = l + column, y = b - 1 - row;
                BOOL inside = x >= left && x < right && y >= top && y < bottom;
                BOOL stroke = FALSE;
                uint64_t step = 0;
                if (pen->style != 5)
                {
                    if (pen->style == 6)
                        stroke = inside && (x - left < diameter || right - 1 - x < diameter ||
                                            y - top < diameter || bottom - 1 - y < diameter);
                    else if (x >= (int64_t)left - before && x <= (int64_t)right - 1 + after &&
                             y >= (int64_t)top - before && y <= (int64_t)bottom - 1 + after)
                    {
                        int64_t a = llabs(y - top), c = llabs(x - ((int64_t)right - 1));
                        int64_t d = llabs(y - ((int64_t)bottom - 1)), e = llabs(x - left);
                        int64_t horizontal = (int64_t)right - left - 1,
                                vertical = (int64_t)bottom - top - 1;
                        if (y - top >= -before && y - top <= after && a <= c && a <= d && a <= e)
                        {
                            stroke = TRUE;
                            step = x > left ? (uint64_t)(x - left) : 0;
                        }
                        else if (x - ((int64_t)right - 1) >= -before &&
                                 x - ((int64_t)right - 1) <= after && c <= d && c <= e)
                        {
                            stroke = TRUE;
                            step = (uint64_t)(horizontal + (y > top ? y - top : 0));
                        }
                        else if (y - ((int64_t)bottom - 1) >= -before &&
                                 y - ((int64_t)bottom - 1) <= after && d <= e)
                        {
                            stroke = TRUE;
                            step =
                                (uint64_t)(horizontal + vertical + (x < right ? right - 1 - x : 0));
                        }
                        else if (x - left >= -before && x - left <= after)
                        {
                            stroke = TRUE;
                            step = (uint64_t)(horizontal * 2 + vertical +
                                              (y < bottom ? bottom - 1 - y : 0));
                        }
                    }
                }
                ULONG color;
                if (stroke)
                {
                    if (dash_pixel(pen->style, step))
                        color = pen->color;
                    else if (dc->background_mode == 2)
                        color = dc->background_color;
                    else
                        continue;
                }
                else if (inside && !lx_guest_load_u32(brush))
                    color = lx_guest_load_u32(brush + 4);
                else
                    continue;
                uint32 rgb = (color & 255) << 16 | (color & 0xFF00) | (color >> 16 & 255);
                size_t index = (size_t)row * width + column;
                pixels[index] = lx_pm_mix_pixel(rgb, pixels[index], mixes[dc->rop2]);
            }
    if (result)
        result =
            lx_pm_raster_write(handle, (LONG)l, canvas_height - (LONG)b, width, height, pixels);
    free(pixels);
    if (!result)
        open32SetError(87);
    return result;
}

static int compare_crossings(const void *a, const void *b)
{
    double left = *(const double *)a, right = *(const double *)b;
    return left < right ? -1 : left > right;
}
static BOOL draw_path(ULONG handle, PVOID input, LONG count, BOOL closed, BOOL filled)
{
    TRACE_NATIVE("GDI path(%u, %p, %d, %u, %u)", handle, input, count, closed, filled);
    Open32DC *dc = open32FindDC(handle);
    Open32Pen *pen = dc ? dc->pen.resource : NULL;
    if (!dc || !pen || !open32CanDraw(handle) || !input || count < 2 || count > 65536)
    {
        open32SetError(87);
        return FALSE;
    }
    POINTL *points = malloc((size_t)count * sizeof *points);
    double *crossings = malloc((size_t)count * sizeof *crossings);
    if (!points || !crossings)
    {
        free(points);
        free(crossings);
        open32SetError(8);
        return FALSE;
    }
    int64_t left = INT32_MAX, top = INT32_MAX, right = INT32_MIN, bottom = INT32_MIN;
    for (LONG i = 0; i < count; i++)
    {
        const uint8 *wire = (const uint8 *)input + i * 8;
        points[i] = (POINTL){(LONG)lx_guest_load_u32(wire), (LONG)lx_guest_load_u32(wire + 4)};
        if (points[i].x < left)
            left = points[i].x;
        if (points[i].x > right)
            right = points[i].x;
        if (points[i].y < top)
            top = points[i].y;
        if (points[i].y > bottom)
            bottom = points[i].y;
    }
    LONG diameter = pen->width ? pen->width : 1, before = (diameter - 1) / 2, after = diameter / 2;
    BOOL valid = right - left <= 65536 && bottom - top <= 65536;
    uint64 work = 0;
    LONG segments = closed ? count : count - 1;
    for (LONG i = 0; valid && i < segments && pen->style != 5; i++)
    {
        POINTL a = points[i], b = points[(i + 1) % count];
        uint64 dx = (uint64)llabs((int64_t)a.x - b.x), dy = (uint64)llabs((int64_t)a.y - b.y);
        work += (dx > dy ? dx : dy) * diameter * diameter;
        valid = work <= 67108864;
    }
    left -= before;
    top -= before;
    right += after + 1;
    bottom += after + 1;
    LONG canvas_width = open32DCWidth(handle), canvas_height = open32DCHeight(handle);
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right > canvas_width)
        right = canvas_width;
    if (bottom > canvas_height)
        bottom = canvas_height;
    if (!valid || (bottom > top && (uint64)(bottom - top) * (ULONG)count > 67108864))
    {
        free(points);
        free(crossings);
        open32SetError(8);
        return FALSE;
    }
    if (left >= right || top >= bottom)
    {
        free(points);
        free(crossings);
        return TRUE;
    }
    size_t width = (size_t)(right - left), height = (size_t)(bottom - top), area = width * height;
    uint8 *coverage = area <= 16777216 ? calloc(area, 1) : NULL;
    uint32 *pixels = coverage ? malloc(area * sizeof *pixels) : NULL;
    if (!coverage || !pixels)
    {
        free(points);
        free(crossings);
        free(coverage);
        free(pixels);
        open32SetError(8);
        return FALSE;
    }
    for (LONG y = (LONG)top; filled && y < bottom; y++)
    {
        unsigned n = 0;
        double scan = y + 0.5;
        for (LONG i = 0; i < count; i++)
        {
            POINTL a = points[i], b = points[(i + 1) % count];
            if ((a.y <= scan && b.y > scan) || (b.y <= scan && a.y > scan))
                crossings[n++] = a.x + (scan - a.y) * ((double)b.x - a.x) / ((double)b.y - a.y);
        }
        qsort(crossings, n, sizeof *crossings, compare_crossings);
        for (unsigned i = 0; i + 1 < n; i += 2)
        {
            int64_t begin = (int64_t)ceil(crossings[i] - 0.5),
                    end = (int64_t)ceil(crossings[i + 1] - 0.5);
            if (begin < left)
                begin = left;
            if (end > right)
                end = right;
            for (int64_t x = begin; x < end; x++)
                coverage[(size_t)(bottom - 1 - y) * width + (size_t)(x - left)] = 1;
        }
    }
    uint64 step = 0;
    for (LONG i = 0; i < segments && pen->style != 5; i++)
    {
        POINTL a = points[i], b = points[(i + 1) % count];
        int64_t x = a.x, y = a.y, dx = llabs((int64_t)b.x - a.x), dy = llabs((int64_t)b.y - a.y),
                error = dx - dy;
        LONG sx = a.x < b.x ? 1 : -1, sy = a.y < b.y ? 1 : -1;
        while (x != b.x || y != b.y)
        {
            uint8 value = dash_pixel(pen->style, step++) ? 2 : dc->background_mode == 2 ? 3 : 0;
            if (value)
                for (LONG oy = -before; oy <= after; oy++)
                    for (LONG ox = -before; ox <= after; ox++)
                    {
                        LONG cx = 2 * (ox + before) - diameter + 1,
                             cy = 2 * (oy + before) - diameter + 1;
                        if (diameter > 1 && cx * cx + cy * cy > diameter * diameter)
                            continue;
                        int64_t px = x + ox, py = y + oy;
                        if (px >= left && px < right && py >= top && py < bottom)
                            coverage[(size_t)(bottom - 1 - py) * width + (size_t)(px - left)] =
                                value;
                    }
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
    }
    BOOL result = lx_pm_raster_read(handle, (LONG)left, canvas_height - (LONG)bottom,
                                    (unsigned)width, (unsigned)height, pixels);
    static const LONG mixes[] = {0, 9, 10, 7, 14, 8, 12, 4, 16, 6, 11, 5, 15, 2, 13, 1, 17};
    if (result)
    {
        for (size_t i = 0; i < area; i++)
        {
            uint32 rgb;
            if (!coverage[i])
                continue;
            if (coverage[i] == 1)
            {
                if (!open32BrushPixel(dc, (LONG)(left + i % width), (LONG)(bottom - 1 - i / width),
                                      &rgb))
                    continue;
            }
            else
            {
                ULONG color = coverage[i] == 2 ? pen->color : dc->background_color;
                rgb = (color & 255) << 16 | (color & 0xff00) | (color >> 16 & 255);
            }
            pixels[i] = lx_pm_mix_pixel(rgb, pixels[i], mixes[dc->rop2]);
        }
        result = lx_pm_raster_write(handle, (LONG)left, canvas_height - (LONG)bottom,
                                    (unsigned)width, (unsigned)height, pixels);
    }
    free(points);
    free(crossings);
    free(coverage);
    free(pixels);
    return result;
}

BOOL Polygon(ULONG handle, PVOID input, LONG count)
{
    return draw_path(handle, input, count, TRUE, TRUE);
}
BOOL Polyline(ULONG handle, PVOID input, LONG count)
{
    return draw_path(handle, input, count, FALSE, FALSE);
}
