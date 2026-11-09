/* Public domain. No rights reserved. Wholly AI-authored. */
/* Lines and shapes with the selected pen and brush: LineTo, Rectangle, polygons, polylines,
   ellipses, arcs, pies, chords and round rectangles. Strokes apply one Boolean mix per covered
   pel. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/pens.h"
#include "os2/ps/awe/gdi/paths.h"
#include "os2/include/private/pmgrep.h"
#include <math.h>

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
    if (open32PathLine(dc, x, y, TRUE) < 0)
        return FALSE;
    if (!dc->path)
    {
        const LONG values[] = {x, y};
        open32MetafileWords(handle, 0x214, 2, values);
    }
    dc->position_x = x;
    dc->position_y = y;
    return TRUE;
}

static BOOL dash_pixel(const Open32Pen *pen, uint64_t step)
{
    LONG style = pen->style;
    if (style == PS_ALTERNATE)
        return !(step & 1);
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

/* Wide strokes with explicit ends or joins, and very wide strokes, are built from filled convex
   polygons in the coverage plane, in device coordinates with y increasing downward. */
typedef struct StrokePlane
{
    uint8 *coverage;
    int64_t left, top, right, bottom;
    size_t width;
} StrokePlane;

static BOOL stroke_polygonal(const Open32Pen *pen)
{
    LONG diameter = pen->width ? pen->width : 1;
    return diameter > 1 && (pen->cap || pen->join || diameter > 32);
}

typedef struct PlaneSpans
{
    StrokePlane *plane;
    uint8 value;
} PlaneSpans;

static BOOL plane_span(void *context, LONG y, LONG left, LONG right)
{
    PlaneSpans *spans = context;
    StrokePlane *plane = spans->plane;
    int64_t begin = left < plane->left ? plane->left : left,
            end = right > plane->right ? plane->right : right;
    for (int64_t x = begin; x < end; x++)
        plane
            ->coverage[(size_t)(plane->bottom - 1 - y) * plane->width + (size_t)(x - plane->left)] =
            spans->value;
    return TRUE;
}

/* Every polygon of a stroke is convex, so the non-zero rule gives its full span on each row. */
static void fill_convex(StrokePlane *plane, const double *points, unsigned count, uint8 value)
{
    PmScanPoint polygon[32];
    size_t counts[1] = {count};
    for (unsigned i = 0; i < count; i++)
        polygon[i] = (PmScanPoint){points[i * 2], points[i * 2 + 1]};
    PlaneSpans spans = {plane, value};
    lx_pm_scan_polygons(polygon, counts, 1, LX_PM_SCAN_WINDING, (LONG)plane->top,
                        (LONG)plane->bottom, plane_span, &spans);
}

static void fill_disc(StrokePlane *plane, double x, double y, double radius, uint8 value)
{
    double points[64];
    for (unsigned i = 0; i < 32; i++)
    {
        double angle = i * 3.14159265358979323846 / 16;
        points[i * 2] = x + radius * cos(angle);
        points[i * 2 + 1] = y + radius * sin(angle);
    }
    fill_convex(plane, points, 32, value);
}

/* One span of a segment, with its end treatment. */
static void stroke_span(StrokePlane *plane, double ax, double ay, double bx, double by, double ux,
                        double uy, double half, LONG start_cap, LONG end_cap, uint8 value)
{
    if (start_cap == 1)
    {
        ax -= ux * half;
        ay -= uy * half;
    }
    if (end_cap == 1)
    {
        bx += ux * half;
        by += uy * half;
    }
    double nx = -uy * half, ny = ux * half;
    double quad[8] = {ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, ax - nx, ay - ny};
    fill_convex(plane, quad, 4, value);
    if (start_cap == 0)
        fill_disc(plane, ax, ay, half, value);
    if (end_cap == 0)
        fill_disc(plane, bx, by, half, value);
}

static void stroke_join_at(StrokePlane *plane, double x, double y, double ux, double uy, double vx,
                           double vy, double half, LONG join, double limit, uint8 value)
{
    double cross = ux * vy - uy * vx, dot = ux * vx + uy * vy;
    if (join == 0)
    {
        fill_disc(plane, x, y, half, value);
        return;
    }
    if (fabs(cross) < 1e-9 && dot > 0)
        return;
    /* The outer side of the turn is opposite the direction of the bend. */
    double side = cross > 0 ? -1 : 1;
    double ax = x - uy * half * side, ay = y + ux * half * side;
    double bx = x - vy * half * side, by = y + vx * half * side;
    double wedge[8] = {x, y, ax, ay, bx, by, 0, 0};
    unsigned count = 3;
    /* The miter ratio is 1 / sin(theta / 2), with theta the interior angle. */
    double ratio = sqrt(2 / fmax(1 + dot, 1e-12));
    if (join == 2 && ratio <= limit)
    {
        double mx = ax + bx - 2 * x, my = ay + by - 2 * y, length = hypot(mx, my);
        if (length > 0)
        {
            wedge[4] = x + mx / length * half * ratio;
            wedge[5] = y + my / length * half * ratio;
            wedge[6] = bx;
            wedge[7] = by;
            count = 4;
        }
    }
    fill_convex(plane, wedge, count, value);
}

/* Dash runs follow the same per-pel pattern as thin lines, measured along each segment. */
static void stroke_polyline(StrokePlane *plane, Open32DC *dc, const Open32Pen *pen,
                            const POINTL *points, LONG count, BOOL closed, uint8 ink, uint8 gap)
{
    double half = (pen->width ? pen->width : 1) / 2.0;
    LONG cap = (LONG)pen->cap, join = (LONG)pen->join;
    double limit = dc->miter_limit > 0 ? dc->miter_limit : 10;
    BOOL dashed =
        pen->style >= 1 && pen->style != PS_NULL && pen->style != PS_INSIDEFRAME && pen->style <= 8;
    LONG segments = closed ? count : count - 1;
    uint64 step = 0;
    for (LONG i = 0; i < segments; i++)
    {
        POINTL a = points[i], b = points[(i + 1) % count];
        double dx = (double)b.x - a.x, dy = (double)b.y - a.y, length = hypot(dx, dy);
        if (!length)
            continue;
        double ux = dx / length, uy = dy / length;
        BOOL first = !closed && i == 0, last = !closed && i == segments - 1;
        if (!dashed)
            stroke_span(plane, a.x, a.y, b.x, b.y, ux, uy, half, first ? cap : 2, last ? cap : 2,
                        ink);
        else
            for (double at = 0; at < length;)
            {
                BOOL on = dash_pixel(pen, step);
                double end = at;
                while (end < length && dash_pixel(pen, step) == on)
                {
                    end += 1;
                    step++;
                }
                if (end > length)
                    end = length;
                uint8 value = on ? ink : gap;
                if (value)
                    stroke_span(plane, a.x + ux * at, a.y + uy * at, a.x + ux * end, a.y + uy * end,
                                ux, uy, half, on ? cap : 2, on ? cap : 2, value);
                at = end;
            }
        if (i + 1 < segments || closed)
        {
            POINTL c = points[(i + 2) % count];
            double ex = (double)c.x - b.x, ey = (double)c.y - b.y, next = hypot(ex, ey);
            if (next)
                stroke_join_at(plane, b.x, b.y, ux, uy, ex / next, ey / next, half, join, limit,
                               ink);
        }
    }
}

/* Thin strokes stamp the pen's square or disc of pels at each pel of the line. */
typedef struct ThinStroke
{
    StrokePlane *plane;
    const Open32Pen *pen;
    LONG before, after, diameter;
    uint8 ink, gap;
    uint64 step;
} ThinStroke;

static BOOL thin_pel(void *context, LONG x, LONG y)
{
    ThinStroke *stroke = context;
    StrokePlane *plane = stroke->plane;
    uint8 value = dash_pixel(stroke->pen, stroke->step++) ? stroke->ink : stroke->gap;
    if (!value)
        return TRUE;
    LONG before = stroke->before, after = stroke->after, diameter = stroke->diameter;
    for (LONG oy = -before; oy <= after; oy++)
        for (LONG ox = -before; ox <= after; ox++)
        {
            LONG cx = 2 * (ox + before) - diameter + 1, cy = 2 * (oy + before) - diameter + 1;
            if (diameter > 1 && cx * cx + cy * cy > diameter * diameter)
                continue;
            int64_t px = (int64_t)x + ox, py = (int64_t)y + oy;
            if (px >= plane->left && px < plane->right && py >= plane->top && py < plane->bottom)
                plane->coverage[(size_t)(plane->bottom - 1 - py) * plane->width +
                                (size_t)(px - plane->left)] = value;
        }
    return TRUE;
}

/* The PM foreground mix that applies each Win32 binary raster operation (R2_*). */
static LONG rop2_mix(LONG rop2)
{
    static const LONG mixes[] = {[R2_BLACK] = FM_ZERO,
                                 [R2_NOTMERGEPEN] = FM_NOTMERGESRC,
                                 [R2_MASKNOTPEN] = FM_SUBTRACT,
                                 [R2_NOTCOPYPEN] = FM_NOTCOPYSRC,
                                 [R2_MASKPENNOT] = FM_MASKSRCNOT,
                                 [R2_NOT] = FM_INVERT,
                                 [R2_XORPEN] = FM_XOR,
                                 [R2_NOTMASKPEN] = FM_NOTMASKSRC,
                                 [R2_MASKPEN] = FM_AND,
                                 [R2_NOTXORPEN] = FM_NOTXORSRC,
                                 [R2_NOP] = FM_LEAVEALONE,
                                 [R2_MERGENOTPEN] = FM_MERGENOTSRC,
                                 [R2_COPYPEN] = FM_OVERPAINT,
                                 [R2_MERGEPENNOT] = FM_MERGESRCNOT,
                                 [R2_MERGEPEN] = FM_OR,
                                 [R2_WHITE] = FM_ONE};
    return mixes[rop2];
}

static BOOL pen_pixel(Open32DC *dc, Open32Pen *pen, LONG x, LONG y, uint32 *rgb)
{
    if (pen->brush.resource)
    {
        Open32DC temporary = *dc;
        temporary.brush = pen->brush;
        return open32BrushPixel(&temporary, x, y, rgb);
    }
    ULONG color = pen->color;
    *rgb = (color & 255) << 16 | (color & 0xff00) | (color >> 16 & 255);
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
    int captured = open32PathLine(dc, target_x, target_y, FALSE);
    if (captured)
    {
        if (captured > 0)
        {
            dc->position_x = target_x;
            dc->position_y = target_y;
        }
        return captured > 0;
    }
    const LONG recorded[] = {target_x, target_y};
    open32MetafileWords(handle, 0x213, 2, recorded);
    Open32Pen *pen = dc->pen.resource;
    LONG x = dc->position_x, y = dc->position_y;
    LONG logical_target_x = target_x, logical_target_y = target_y;
    if (!open32MapPoint(dc, &x, &y, TRUE) || !open32MapPoint(dc, &target_x, &target_y, TRUE))
        return FALSE;
    int64_t dx = llabs((int64_t)target_x - x), dy = llabs((int64_t)target_y - y);
    if (dx > 65536 || dy > 65536)
    {
        open32SetError(87);
        return FALSE;
    }
    if (pen->style == PS_NULL || (!dx && !dy))
    {
        dc->position_x = logical_target_x;
        dc->position_y = logical_target_y;
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
        dc->position_x = logical_target_x;
        dc->position_y = logical_target_y;
        return TRUE;
    }
    uint64_t pixel_count = (uint64_t)(right - left) * (uint64_t)(bottom - top);
    uint64_t work = (uint64_t)((dx > dy ? dx : dy) + 1) * diameter * diameter;
    BOOL polygonal = stroke_polygonal(pen);
    if (pixel_count > 16777216 || (!polygonal && work > 67108864))
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
    StrokePlane plane = {coverage, left, top, right, bottom, width};
    uint8 gap = dc->background_mode == OPAQUE ? 2 : 0;
    if (polygonal)
        stroke_polyline(&plane, dc, pen, (POINTL[2]){{x, y}, {target_x, target_y}}, 2, FALSE, 1,
                        gap);
    else
    {
        /* The final point is excluded, matching Windows LineTo and joined segments. */
        ThinStroke thin = {&plane, pen, before, after, diameter, 1, gap, 0};
        lx_pm_walk_line(x, y, target_x, target_y, FALSE, thin_pel, &thin);
    }
    BOOL result = lx_pm_raster_read(handle, (LONG)left, canvas_height - (LONG)bottom,
                                    (unsigned)width, (unsigned)height, pixels);
    if (result)
    {
        for (size_t i = 0; i < area; i++)
            if (coverage[i])
            {
                ULONG color = coverage[i] == 1 ? pen->color : dc->background_color;
                ULONG rgb = (color & 255) << 16 | (color & 0xFF00) | (color >> 16 & 255);
                if (coverage[i] == 1 && !pen_pixel(dc, pen, (LONG)(left + i % width),
                                                   (LONG)(bottom - 1 - i / width), &rgb))
                    continue;
                pixels[i] = lx_pm_mix_pixel(rgb, pixels[i], rop2_mix(dc->rop2));
            }
        result = lx_pm_raster_write(handle, (LONG)left, canvas_height - (LONG)bottom,
                                    (unsigned)width, (unsigned)height, pixels);
    }
    free(pixels);
    free(coverage);
    if (result)
    {
        dc->position_x = logical_target_x;
        dc->position_y = logical_target_y;
    }
    else
        open32SetError(87);
    return result;
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
    if (!open32IdentityMapping(dc) || stroke_polygonal(dc->pen.resource))
    {
        uint8 points[32];
        LONG xy[8] = {left, top, right, top, right, bottom, left, bottom};
        for (unsigned i = 0; i < 8; i++)
            lx_guest_store_u32(points + i * 4, (ULONG)xy[i]);
        return Polygon(handle, points, 4);
    }
    uint8 path_rectangle[32];
    LONG path_coords[8] = {left, top, right, top, right, bottom, left, bottom};
    for (unsigned i = 0; i < 8; i++)
        lx_guest_store_u32(path_rectangle + i * 4, (ULONG)path_coords[i]);
    int captured = open32PathCapture(dc, path_rectangle, 4, TRUE);
    if (captured)
        return captured > 0;
    const LONG recorded[] = {left, top, right, bottom};
    open32MetafileWords(handle, 0x41b, 4, recorded);
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
    LONG before = pen->style == PS_INSIDEFRAME ? 0 : (diameter - 1) / 2;
    LONG after = pen->style == PS_INSIDEFRAME ? 0 : diameter / 2;
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
    if (result)
        for (unsigned row = 0; row < height; row++)
            for (unsigned column = 0; column < width; column++)
            {
                int64_t x = l + column, y = b - 1 - row;
                BOOL inside = x >= left && x < right && y >= top && y < bottom;
                BOOL stroke = FALSE;
                uint64_t step = 0;
                if (pen->style != PS_NULL)
                {
                    if (pen->style == PS_INSIDEFRAME)
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
                    if (dash_pixel(pen, step))
                        color = pen->color;
                    else if (dc->background_mode == OPAQUE)
                        color = dc->background_color;
                    else
                        continue;
                }
                else
                {
                    uint32 fill;
                    if (inside && open32BrushPixel(dc, (LONG)x, (LONG)y, &fill))
                    {
                        size_t filled = (size_t)row * width + column;
                        pixels[filled] = lx_pm_mix_pixel(fill, pixels[filled], rop2_mix(dc->rop2));
                    }
                    continue;
                }
                uint32 rgb = (color & 255) << 16 | (color & 0xFF00) | (color >> 16 & 255);
                if (stroke && dash_pixel(pen, step) && !pen_pixel(dc, pen, (LONG)x, (LONG)y, &rgb))
                    continue;
                size_t index = (size_t)row * width + column;
                pixels[index] = lx_pm_mix_pixel(rgb, pixels[index], rop2_mix(dc->rop2));
            }
    if (result)
        result =
            lx_pm_raster_write(handle, (LONG)l, canvas_height - (LONG)b, width, height, pixels);
    free(pixels);
    if (!result)
        open32SetError(87);
    return result;
}

static BOOL draw_path(ULONG handle, PVOID input, LONG count, BOOL closed, BOOL filled)
{
    TRACE_NATIVE("GDI path(%u, %p, %d, %u, %u)", handle, input, count, closed, filled);
    if (GProcessState.trace_native && input && count > 0)
        for (LONG i = 0; i < count && i < 8; i++)
            TRACE_NATIVE("  point %d: %d,%d of height %d", (int)i,
                         (int)lx_guest_load_u32((const uint8 *)input + i * 8),
                         (int)lx_guest_load_u32((const uint8 *)input + i * 8 + 4),
                         (int)open32DCHeight(handle));
    Open32DC *dc = open32FindDC(handle);
    Open32Pen *pen = dc ? dc->pen.resource : NULL;
    if (!dc || !pen || !open32CanDraw(handle) || !input || count < 2 || count > 65536)
    {
        open32SetError(87);
        return FALSE;
    }
    int captured = open32PathCapture(dc, input, count, closed);
    if (captured)
        return captured > 0;
    open32MetafilePoints(handle, closed ? 0x324 : 0x325, input, count);
    POINTL *points = malloc((size_t)count * sizeof *points);
    PmScanPoint *shape = malloc((size_t)count * sizeof *shape);
    if (!points || !shape)
    {
        free(points);
        free(shape);
        open32SetError(8);
        return FALSE;
    }
    int64_t left = INT32_MAX, top = INT32_MAX, right = INT32_MIN, bottom = INT32_MIN;
    for (LONG i = 0; i < count; i++)
    {
        const uint8 *wire = (const uint8 *)input + i * 8;
        points[i] = (POINTL){(LONG)lx_guest_load_u32(wire), (LONG)lx_guest_load_u32(wire + 4)};
        if (!open32MapPoint(dc, &points[i].x, &points[i].y, TRUE))
        {
            free(points);
            free(shape);
            return FALSE;
        }
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
    BOOL polygonal = stroke_polygonal(pen);
    uint64 work = 0;
    LONG segments = closed ? count : count - 1;
    for (LONG i = 0; valid && !polygonal && i < segments && pen->style != PS_NULL; i++)
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
        free(shape);
        open32SetError(8);
        return FALSE;
    }
    if (left >= right || top >= bottom)
    {
        free(points);
        free(shape);
        return TRUE;
    }
    size_t width = (size_t)(right - left), height = (size_t)(bottom - top), area = width * height;
    uint8 *coverage = area <= 16777216 ? calloc(area, 1) : NULL;
    uint32 *pixels = coverage ? malloc(area * sizeof *pixels) : NULL;
    if (!coverage || !pixels)
    {
        free(points);
        free(shape);
        free(coverage);
        free(pixels);
        open32SetError(8);
        return FALSE;
    }
    StrokePlane plane = {coverage, left, top, right, bottom, width};
    if (filled)
    {
        for (LONG i = 0; i < count; i++)
            shape[i] = (PmScanPoint){points[i].x, points[i].y};
        size_t contour[1] = {(size_t)count};
        PlaneSpans spans = {&plane, 1};
        lx_pm_scan_polygons(shape, contour, 1,
                            dc->poly_fill_mode == WINDING ? LX_PM_SCAN_WINDING
                                                          : LX_PM_SCAN_ALTERNATE,
                            (LONG)top, (LONG)bottom, plane_span, &spans);
    }
    uint8 gap = dc->background_mode == OPAQUE ? 3 : 0;
    if (polygonal && pen->style != PS_NULL)
        stroke_polyline(&plane, dc, pen, points, count, closed, 2, gap);
    ThinStroke thin = {&plane, pen, before, after, diameter, 2, gap, 0};
    for (LONG i = 0; !polygonal && i < segments && pen->style != PS_NULL; i++)
    {
        POINTL a = points[i], b = points[(i + 1) % count];
        lx_pm_walk_line(a.x, a.y, b.x, b.y, FALSE, thin_pel, &thin);
    }
    BOOL result = lx_pm_raster_read(handle, (LONG)left, canvas_height - (LONG)bottom,
                                    (unsigned)width, (unsigned)height, pixels);
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
                if (coverage[i] == 2 && !pen_pixel(dc, pen, (LONG)(left + i % width),
                                                   (LONG)(bottom - 1 - i / width), &rgb))
                    continue;
            }
            pixels[i] = lx_pm_mix_pixel(rgb, pixels[i], rop2_mix(dc->rop2));
        }
        result = lx_pm_raster_write(handle, (LONG)left, canvas_height - (LONG)bottom,
                                    (unsigned)width, (unsigned)height, pixels);
    }
    free(points);
    free(shape);
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

static BOOL curve(ULONG h, LONG l, LONG t, LONG r, LONG b, LONG sx, LONG sy, LONG ex, LONG ey,
                  unsigned kind)
{
    if (!open32FindDC(h))
    {
        open32SetError(6);
        return FALSE;
    }
    if (l > r)
    {
        LONG v = l;
        l = r;
        r = v;
    }
    if (t > b)
    {
        LONG v = t;
        t = b;
        b = v;
    }
    double rx = ((double)r - l - 1) / 2, ry = ((double)b - t - 1) / 2, cx = l + rx, cy = t + ry;
    if (rx < 0 || ry < 0)
        return TRUE;
    if (rx > 32768 || ry > 32768)
    {
        open32SetError(87);
        return FALSE;
    }
    double start = 0, sweep = 2 * 3.14159265358979323846;
    Open32DC *mapping = open32FindDC(h);
    if (kind != 0)
    {
        start = atan2((cy - sy) / (ry ? ry : 1), (sx - cx) / (rx ? rx : 1));
        double end = atan2((cy - ey) / (ry ? ry : 1), (ex - cx) / (rx ? rx : 1));
        sweep = end - start;
        if (mapping->arc_clockwise)
        {
            if (sweep >= 0)
                sweep -= 2 * 3.14159265358979323846;
        }
        else if (sweep <= 0)
            sweep += 2 * 3.14159265358979323846;
    }
    unsigned steps = (unsigned)ceil(fabs(sweep) * sqrt(fmax(rx, ry) + 1) * 2);
    if (steps < 12)
        steps = 12;
    if (steps > 4096)
        steps = 4096;
    ULONG n = steps + 1 + (kind == 2 ? 1 : 0);
    uint8 *points = malloc((size_t)n * 8);
    if (!points)
    {
        open32SetError(8);
        return FALSE;
    }
    for (unsigned i = 0; i <= steps; i++)
    {
        double a = start + sweep * i / steps;
        lx_guest_store_u32(points + i * 8, (ULONG)(LONG)lround(cx + rx * cos(a)));
        lx_guest_store_u32(points + i * 8 + 4, (ULONG)(LONG)lround(cy - ry * sin(a)));
    }
    if (kind == 2)
    {
        lx_guest_store_u32(points + (n - 1) * 8, (ULONG)(LONG)lround(cx));
        lx_guest_store_u32(points + (n - 1) * 8 + 4, (ULONG)(LONG)lround(cy));
    }
    BOOL ok = TRUE;
    if (kind == 4)
        for (ULONG i = 0; ok && i < n; i++)
            ok = LineTo(h, (LONG)lx_guest_load_u32(points + i * 8),
                        (LONG)lx_guest_load_u32(points + i * 8 + 4));
    else
        ok = kind == 1 ? Polyline(h, points, (LONG)n) : Polygon(h, points, (LONG)n);
    free(points);
    return ok;
}

BOOL Ellipse(ULONG h, LONG l, LONG t, LONG r, LONG b)
{
    return curve(h, l, t, r, b, 0, 0, 0, 0, 0);
}

BOOL Arc(ULONG h, LONG l, LONG t, LONG r, LONG b, LONG sx, LONG sy, LONG ex, LONG ey)
{
    return curve(h, l, t, r, b, sx, sy, ex, ey, 1);
}

BOOL Pie(ULONG h, LONG l, LONG t, LONG r, LONG b, LONG sx, LONG sy, LONG ex, LONG ey)
{
    return curve(h, l, t, r, b, sx, sy, ex, ey, 2);
}

BOOL open32ArcTo(ULONG h, LONG l, LONG t, LONG r, LONG b, LONG sx, LONG sy, LONG ex, LONG ey)
{
    return curve(h, l, t, r, b, sx, sy, ex, ey, 4);
}

BOOL open32Chord(ULONG h, LONG l, LONG t, LONG r, LONG b, LONG sx, LONG sy, LONG ex, LONG ey)
{
    return curve(h, l, t, r, b, sx, sy, ex, ey, 3);
}

BOOL RoundRect(ULONG h, LONG l, LONG t, LONG r, LONG b, LONG ew, LONG eh)
{
    if (l > r)
    {
        LONG v = l;
        l = r;
        r = v;
    }
    if (t > b)
    {
        LONG v = t;
        t = b;
        b = v;
    }
    if ((int64_t)r - l > 65536 || (int64_t)b - t > 65536)
    {
        open32SetError(87);
        return FALSE;
    }
    double rx = fmin(fabs((double)ew), (double)r - l) / 2,
           ry = fmin(fabs((double)eh), (double)b - t) / 2;
    if (!rx || !ry)
        return Rectangle(h, l, t, r, b);
    uint8 points[4 * 65 * 8];
    unsigned used = 0;
    for (unsigned corner = 0; corner < 4; corner++)
    {
        double cx = corner == 0 || corner == 3 ? r - rx - 1 : l + rx,
               cy = corner < 2 ? t + ry : b - ry - 1;
        for (unsigned i = 0; i <= 64; i++)
        {
            double a = (corner * 0.5 + i / 128.0) * 3.14159265358979323846;
            lx_guest_store_u32(points + used * 8, (ULONG)(LONG)lround(cx + rx * cos(a)));
            lx_guest_store_u32(points + used * 8 + 4, (ULONG)(LONG)lround(cy - ry * sin(a)));
            used++;
        }
    }
    return Polygon(h, points, (LONG)used);
}

BOOL open32AngleArc(ULONG h, LONG x, LONG y, ULONG radius, float start, float sweep)
{
    double first = start * 3.14159265358979323846 / 180,
           range = sweep * 3.14159265358979323846 / 180;
    unsigned steps = (unsigned)ceil(fabs(range) * sqrt((double)radius + 1) * 2);
    steps = steps < 12 ? 12 : steps > 4096 ? 4096 : steps;
    BOOL ok = TRUE;
    for (unsigned i = 0; ok && i <= steps; i++)
    {
        double a = first + range * i / steps;
        ok = LineTo(h, (LONG)lround(x + radius * cos(a)), (LONG)lround(y - radius * sin(a)));
    }
    return ok;
}

BOOL PolyPolyline(ULONG h, PVOID points, PVOID counts, ULONG polygons)
{
    if (!points || !counts || polygons > 65536)
    {
        open32SetError(87);
        return FALSE;
    }
    size_t offset = 0;
    for (ULONG p = 0; p < polygons; p++)
    {
        ULONG n = lx_guest_load_u32((uint8 *)counts + p * 4);
        if (n < 2 || n > 65536 - offset)
        {
            open32SetError(87);
            return FALSE;
        }
        if (!Polyline(h, (uint8 *)points + offset * 8, (LONG)n))
            return FALSE;
        offset += n;
    }
    return TRUE;
}

BOOL PolyPolygon(ULONG h, PVOID points, PVOID counts, LONG polygons)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || !points || !counts || polygons < 0 || polygons > 65536)
    {
        open32SetError(87);
        return FALSE;
    }
    if (open32PathRecording(dc))
    {
        size_t offset = 0;
        for (LONG p = 0; p < polygons; p++)
        {
            LONG n = (LONG)lx_guest_load_u32((uint8 *)counts + p * 4);
            if (n < 2 || n > 65536 - offset ||
                open32PathCapture(dc, (uint8 *)points + offset * 8, n, TRUE) < 0)
                return FALSE;
            offset += (size_t)n;
        }
        return TRUE;
    }
    if (open32IsMetafile(h))
        return open32MetafilePolyPolygon(h, points, counts, polygons);
    ULONG region = CreatePolyPolygonRgn(points, counts, polygons, dc->poly_fill_mode);
    if (!region)
        return FALSE;
    BOOL ok = PaintRgn(h, region);
    DeleteObject(region);
    size_t offset = 0;
    for (LONG p = 0; ok && p < polygons; p++)
    {
        LONG n = (LONG)lx_guest_load_u32((uint8 *)counts + p * 4);
        uint8 *closed = malloc(((size_t)n + 1) * 8);
        if (!closed)
        {
            open32SetError(8);
            return FALSE;
        }
        memcpy(closed, (uint8 *)points + offset * 8, (size_t)n * 8);
        memcpy(closed + n * 8, closed, 8);
        ok = Polyline(h, closed, n + 1);
        free(closed);
        offset += (size_t)n;
    }
    return ok;
}

BOOL LineDDA(LONG x, LONG y, LONG end_x, LONG end_y, ULONG procedure, ULONG parameter)
{
    if (!lx_execution_memory(GProcessState.execution, procedure, 1, LX_GUEST_EXECUTE))
    {
        open32SetError(87);
        return FALSE;
    }
    int64_t dx = llabs((int64_t)end_x - x), dy = llabs((int64_t)end_y - y);
    if (dx > 1048576 || dy > 1048576)
    {
        open32SetError(87);
        return FALSE;
    }
    LONG sx = x < end_x ? 1 : -1, sy = y < end_y ? 1 : -1;
    int64_t error = dx - dy;
    while (x != end_x || y != end_y)
    {
        lx_execution_callback32_4(GProcessState.execution, procedure, (ULONG)x, (ULONG)y, parameter,
                                  0);
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
    return TRUE;
}

BOOL DrawFocusRect(ULONG h, PVOID input)
{
    if (!input || !open32FindDC(h))
    {
        open32SetError(87);
        return FALSE;
    }
    LONG l = (LONG)lx_guest_load_u32(input), t = (LONG)lx_guest_load_u32((uint8 *)input + 4),
         r = (LONG)lx_guest_load_u32((uint8 *)input + 8),
         b = (LONG)lx_guest_load_u32((uint8 *)input + 12);
    if (l >= r || t >= b)
        return TRUE;
    ULONG pen = CreatePen(2, 0, 0xffffff);
    if (!pen)
        return FALSE;
    Open32DC *dc = open32FindDC(h);
    ULONG old = open32SelectPen(dc, pen);
    LONG rop = SetROP2(h, 7), background = SetBkMode(h, 1);
    uint8 p[40];
    LONG coords[10] = {l, t, r - 1, t, r - 1, b - 1, l, b - 1, l, t};
    for (unsigned i = 0; i < 10; i++)
        lx_guest_store_u32(p + i * 4, (ULONG)coords[i]);
    BOOL ok = Polyline(h, p, 5);
    SetBkMode(h, background);
    SetROP2(h, rop);
    open32SelectPen(dc, old);
    DeleteObject(pen);
    return ok;
}

/* A seed fill over the whole drawable, painted with the current brush. Mode 0 stops at the given
   border color; mode 1 replaces the surface color. */
BOOL open32FloodFill(ULONG h, LONG x, LONG y, ULONG color, ULONG mode)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || !open32CanDraw(h) || !open32MapPoint(dc, &x, &y, TRUE))
    {
        open32SetError(6);
        return FALSE;
    }
    LONG width = open32DCWidth(h), height = open32DCHeight(h);
    if (x < 0 || y < 0 || x >= width || y >= height || (uint64)width * height > 16777216)
    {
        open32SetError(87);
        return FALSE;
    }
    size_t area = (size_t)width * height;
    uint32 *pixels = malloc(area * 4), *stack = malloc(area * 4);
    uint8 *seen = calloc(area, 1);
    BOOL ok = pixels && stack && seen &&
              lx_pm_raster_read(h, 0, 0, (unsigned)width, (unsigned)height, pixels);
    uint32 rgb = (color & 255) << 16 | (color & 0xff00) | (color >> 16 & 255);
    size_t top = 0, seed = (size_t)(height - 1 - y) * width + x;
    BOOL start = ok && (mode ? pixels[seed] == rgb : pixels[seed] != rgb);
    if (start)
    {
        stack[top++] = (uint32)seed;
        seen[seed] = 1;
    }
    while (top)
    {
        size_t at = stack[--top];
        LONG px = (LONG)(at % width), row = (LONG)(at / width);
        const LONG neighbours[4][2] = {{px - 1, row}, {px + 1, row}, {px, row - 1}, {px, row + 1}};
        for (unsigned i = 0; i < 4; i++)
        {
            LONG nx = neighbours[i][0], ny = neighbours[i][1];
            if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                continue;
            size_t next = (size_t)ny * width + nx;
            if (!seen[next] && (mode ? pixels[next] == rgb : pixels[next] != rgb))
            {
                seen[next] = 1;
                stack[top++] = (uint32)next;
            }
        }
    }
    if (start)
    {
        for (size_t i = 0; i < area; i++)
            if (seen[i])
            {
                uint32 fill;
                if (open32BrushPixel(dc, (LONG)(i % width), height - 1 - (LONG)(i / width), &fill))
                    pixels[i] = fill;
            }
        ok = lx_pm_raster_write(h, 0, 0, (unsigned)width, (unsigned)height, pixels);
    }
    free(pixels);
    free(stack);
    free(seen);
    return ok && start;
}
