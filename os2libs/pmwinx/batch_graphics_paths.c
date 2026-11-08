/* Public domain. No rights reserved. Wholly AI-authored. */
/* Curves are flattened into subpixel-accurate connected GDI line segments. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/batch_graphics_paths.h"
#include "os2libs/pmwin/graphics.h"
#include <math.h>

typedef struct Open32PathPoint
{
    LONG x, y;
    BOOL move;
} Open32PathPoint;

struct Open32Path
{
    Open32PathPoint *points;
    size_t count, capacity;
    BOOL recording, ready, widened;
};

void open32PathCleanup(Open32DC *dc)
{
    if (dc->path)
    {
        free(dc->path->points);
        free(dc->path);
        dc->path = NULL;
    }
}

static BOOL path_point(struct Open32Path *p, LONG x, LONG y, BOOL move)
{
    if (p->count == p->capacity)
    {
        size_t n = p->capacity ? p->capacity * 2 : 64;
        if (n > 1048576)
        {
            open32SetError(8);
            return FALSE;
        }
        void *more = realloc(p->points, n * sizeof *p->points);
        if (!more)
        {
            open32SetError(8);
            return FALSE;
        }
        p->points = more;
        p->capacity = n;
    }
    p->points[p->count++] = (Open32PathPoint){x, y, move};
    return TRUE;
}

int open32PathLine(Open32DC *dc, LONG x, LONG y, BOOL move)
{
    if (!dc || !dc->path || !dc->path->recording)
        return 0;
    struct Open32Path *p = dc->path;
    if (!move && (!p->count || p->points[p->count - 1].x != dc->position_x ||
                  p->points[p->count - 1].y != dc->position_y))
        if (!path_point(p, dc->position_x, dc->position_y, TRUE))
            return -1;
    return path_point(p, x, y, move) ? 1 : -1;
}

int open32PathCapture(Open32DC *dc, PVOID input, LONG count, BOOL close)
{
    if (!dc || !dc->path || !dc->path->recording)
        return 0;
    if (!input || count < 1 || count > 65536)
    {
        open32SetError(87);
        return -1;
    }
    size_t before = dc->path->count;
    for (LONG i = 0; i < count + (close ? 1 : 0); i++)
    {
        const uint8 *p = (uint8 *)input + (i % count) * 8;
        if (!path_point(dc->path, (LONG)lx_guest_load_u32(p), (LONG)lx_guest_load_u32(p + 4),
                        i == 0))
        {
            dc->path->count = before;
            return -1;
        }
    }
    return 1;
}

BOOL BeginPath(ULONG h)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
    {
        open32SetError(6);
        return FALSE;
    }
    open32PathCleanup(dc);
    dc->path = calloc(1, sizeof *dc->path);
    if (!dc->path)
    {
        open32SetError(8);
        return FALSE;
    }
    dc->path->recording = TRUE;
    return TRUE;
}

BOOL EndPath(ULONG h)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || !dc->path || !dc->path->recording)
    {
        open32SetError(dc ? 1003 : 6);
        return FALSE;
    }
    dc->path->recording = FALSE;
    dc->path->ready = TRUE;
    return TRUE;
}

BOOL StrokePath(ULONG h)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || !dc->path || !dc->path->ready)
    {
        open32SetError(dc ? 1003 : 6);
        return FALSE;
    }
    struct Open32Path *p = dc->path;
    dc->path = NULL;
    LONG x = dc->position_x, y = dc->position_y;
    BOOL ok = TRUE;
    for (size_t i = 0; ok && i < p->count; i++)
        ok = p->points[i].move ? MoveToEx(h, p->points[i].x, p->points[i].y, NULL)
                               : LineTo(h, p->points[i].x, p->points[i].y);
    dc->position_x = x;
    dc->position_y = y;
    free(p->points);
    free(p);
    return ok;
}

BOOL open32PathClose(Open32DC *dc)
{
    if (!dc || !dc->path || !dc->path->recording)
    {
        open32SetError(dc ? 1003 : 6);
        return FALSE;
    }
    struct Open32Path *p = dc->path;
    if (!p->count)
        return TRUE;
    size_t start = p->count - 1;
    while (start && !p->points[start].move)
        start--;
    return path_point(p, p->points[start].x, p->points[start].y, FALSE);
}

/* The path's subpaths as one region, in logical or device coordinates. A widened path is a set of
   overlapping pieces that must fill as a union. */
static ULONG path_region(Open32DC *dc, BOOL device)
{
    struct Open32Path *p = dc->path;
    if (p->count > 65536)
    {
        open32SetError(8);
        return 0;
    }
    uint8 *points = malloc((p->count ? p->count : 1) * 8),
          *counts = malloc((p->count ? p->count : 1) * 4);
    BOOL ok = points && counts;
    LONG polygons = 0;
    ULONG used = 0, region = 0;
    for (size_t i = 0; ok && i < p->count; i++)
    {
        if (p->points[i].move || !polygons)
        {
            if (polygons)
                lx_guest_store_u32(counts + (polygons - 1) * 4, used);
            polygons++;
            used = 0;
        }
        LONG x = p->points[i].x, y = p->points[i].y;
        ok = !device || open32MapPoint(dc, &x, &y, TRUE);
        lx_guest_store_u32(points + i * 8, (ULONG)x);
        lx_guest_store_u32(points + i * 8 + 4, (ULONG)y);
        used++;
    }
    if (ok && polygons)
    {
        lx_guest_store_u32(counts + (polygons - 1) * 4, used);
        region =
            CreatePolyPolygonRgn(points, counts, polygons, p->widened ? 2 : dc->poly_fill_mode);
    }
    else if (ok)
        region = CreateRectRgn(0, 0, 0, 0);
    else
        open32SetError(8);
    free(points);
    free(counts);
    return region;
}

/* Subpaths fill together under the DC's polygon fill mode, then the path is consumed. */
BOOL open32PathFill(ULONG h, BOOL stroke)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || !dc->path || !dc->path->ready)
    {
        open32SetError(dc ? 1003 : 6);
        return FALSE;
    }
    ULONG region = path_region(dc, FALSE);
    BOOL ok = region && PaintRgn(h, region);
    if (region)
        DeleteObject(region);
    if (ok && stroke)
        return StrokePath(h);
    open32PathCleanup(dc);
    return ok;
}

BOOL open32PathClip(ULONG h, LONG mode)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || !dc->path || !dc->path->ready)
    {
        open32SetError(dc ? 1003 : 6);
        return FALSE;
    }
    ULONG region = path_region(dc, TRUE);
    BOOL ok = region && ExtSelectClipRgn(h, region, mode) != 0;
    if (region)
        DeleteObject(region);
    open32PathCleanup(dc);
    return ok;
}

/* Each segment becomes a pen-wide quadrilateral and each vertex an octagon, all with one winding. */
BOOL open32PathWiden(Open32DC *dc)
{
    uint8 pen[16];
    if (!dc || !dc->path || !dc->path->ready)
    {
        open32SetError(dc ? 1003 : 6);
        return FALSE;
    }
    if (open32PenObject(dc->pen_handle, sizeof pen, pen) != 16)
        return FALSE;
    double half = fmax((LONG)lx_guest_load_u32(pen + 4), 1) / 2.0;
    struct Open32Path *source = dc->path, *wide = calloc(1, sizeof *wide);
    if (!wide)
    {
        open32SetError(8);
        return FALSE;
    }
    BOOL ok = TRUE;
    for (size_t i = 0; ok && i < source->count; i++)
    {
        const Open32PathPoint *b = &source->points[i];
        for (unsigned k = 0; ok && k < 8; k++)
        {
            double angle = k * 3.14159265358979323846 / 4;
            ok = path_point(wide, (LONG)lround(b->x + half * cos(angle)),
                            (LONG)lround(b->y + half * sin(angle)), k == 0);
        }
        if (!ok || !i || b->move)
            continue;
        const Open32PathPoint *a = &source->points[i - 1];
        double dx = (double)b->x - a->x, dy = (double)b->y - a->y, length = hypot(dx, dy);
        if (!length)
            continue;
        double nx = -dy / length * half, ny = dx / length * half;
        const double corners[4][2] = {{a->x - nx, a->y - ny},
                                      {b->x - nx, b->y - ny},
                                      {b->x + nx, b->y + ny},
                                      {a->x + nx, a->y + ny}};
        for (unsigned k = 0; ok && k < 4; k++)
            ok = path_point(wide, (LONG)lround(corners[k][0]), (LONG)lround(corners[k][1]), k == 0);
    }
    if (!ok)
    {
        free(wide->points);
        free(wide);
        return FALSE;
    }
    wide->ready = wide->widened = TRUE;
    open32PathCleanup(dc);
    dc->path = wide;
    return TRUE;
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

/* Mode 0 replaces the transform; MWT_IDENTITY 1 clears it; MWT_LEFTMULTIPLY 2 and
   MWT_RIGHTMULTIPLY 3 compose with the current one. */
BOOL open32WorldTransform(ULONG h, const double m[6], ULONG mode)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || mode > 3)
    {
        open32SetError(dc ? 87 : 6);
        return FALSE;
    }
    if (mode == 1)
    {
        dc->world_active = FALSE;
        return TRUE;
    }
    double current[6] = {1, 0, 0, 1, 0, 0}, out[6];
    if (dc->world_active)
        memcpy(current, dc->world, sizeof current);
    const double *a = mode == 3 ? current : m, *b = mode == 3 ? m : current;
    if (mode)
    {
        out[0] = a[0] * b[0] + a[1] * b[2];
        out[1] = a[0] * b[1] + a[1] * b[3];
        out[2] = a[2] * b[0] + a[3] * b[2];
        out[3] = a[2] * b[1] + a[3] * b[3];
        out[4] = a[4] * b[0] + a[5] * b[2] + b[4];
        out[5] = a[4] * b[1] + a[5] * b[3] + b[5];
    }
    else
        memcpy(out, m, sizeof out);
    memcpy(dc->world, out, sizeof out);
    dc->world_active = TRUE;
    return TRUE;
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
    if (dc->path && dc->path->recording)
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
