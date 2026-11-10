/* Public domain. No rights reserved. Wholly AI-authored. */
/* Curves are flattened into subpixel-accurate connected GDI line segments. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/paths.h"
#include <math.h>

enum
{
    PATH_INITIAL_POINTS = 64,
    /* Bounds on a recorded path and on one Polyline/Polygon call into it. */
    PATH_POINTS_MAX = 1048576,
    PATH_CALL_POINTS_MAX = 65536,
    /* Polygon vertex counts are 32-bit INTs. */
    INT_BYTES = 4,
    /* Widening rounds each vertex with an octagon. */
    OCTAGON_VERTICES = 8
};

static const double PI = 3.14159265358979323846;

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
        size_t n = p->capacity ? p->capacity * 2 : PATH_INITIAL_POINTS;
        if (n > PATH_POINTS_MAX)
        {
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        void *more = realloc(p->points, n * sizeof *p->points);
        if (!more)
        {
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
    if (!input || count < 1 || count > PATH_CALL_POINTS_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    size_t before = dc->path->count;
    for (LONG i = 0; i < count + (close ? 1 : 0); i++)
    {
        const uint8 *p = (uint8 *)input + (i % count) * WIN32_POINT_SIZE;
        if (!path_point(dc->path, (LONG)lx_guest_load_u32(p + WIN32_POINT_X),
                        (LONG)lx_guest_load_u32(p + WIN32_POINT_Y), i == 0))
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
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    open32PathCleanup(dc);
    dc->path = calloc(1, sizeof *dc->path);
    if (!dc->path)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
        open32SetError(dc ? ERROR_CAN_NOT_COMPLETE : ERROR_INVALID_HANDLE);
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
        open32SetError(dc ? ERROR_CAN_NOT_COMPLETE : ERROR_INVALID_HANDLE);
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
        open32SetError(dc ? ERROR_CAN_NOT_COMPLETE : ERROR_INVALID_HANDLE);
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
    if (p->count > PATH_CALL_POINTS_MAX)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    uint8 *points = malloc((p->count ? p->count : 1) * WIN32_POINT_SIZE),
          *counts = malloc((p->count ? p->count : 1) * INT_BYTES);
    BOOL ok = points && counts;
    LONG polygons = 0;
    ULONG used = 0, region = 0;
    for (size_t i = 0; ok && i < p->count; i++)
    {
        if (p->points[i].move || !polygons)
        {
            if (polygons)
                lx_guest_store_u32(counts + (polygons - 1) * INT_BYTES, used);
            polygons++;
            used = 0;
        }
        LONG x = p->points[i].x, y = p->points[i].y;
        ok = !device || open32MapPoint(dc, &x, &y, TRUE);
        lx_guest_store_u32(points + i * WIN32_POINT_SIZE + WIN32_POINT_X, (ULONG)x);
        lx_guest_store_u32(points + i * WIN32_POINT_SIZE + WIN32_POINT_Y, (ULONG)y);
        used++;
    }
    if (ok && polygons)
    {
        lx_guest_store_u32(counts + (polygons - 1) * INT_BYTES, used);
        region = CreatePolyPolygonRgn(points, counts, polygons,
                                      p->widened ? WINDING : dc->poly_fill_mode);
    }
    else if (ok)
        region = CreateRectRgn(0, 0, 0, 0);
    else
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
        open32SetError(dc ? ERROR_CAN_NOT_COMPLETE : ERROR_INVALID_HANDLE);
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
        open32SetError(dc ? ERROR_CAN_NOT_COMPLETE : ERROR_INVALID_HANDLE);
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
    uint8 pen[WIN32_LOGPEN_SIZE];
    if (!dc || !dc->path || !dc->path->ready)
    {
        open32SetError(dc ? ERROR_CAN_NOT_COMPLETE : ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (open32PenObject(dc->pen_handle, sizeof pen, pen) != WIN32_LOGPEN_SIZE)
        return FALSE;
    double half = fmax((LONG)lx_guest_load_u32(pen + WIN32_LOGPEN_WIDTH), 1) / 2.0;
    struct Open32Path *source = dc->path, *wide = calloc(1, sizeof *wide);
    if (!wide)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    BOOL ok = TRUE;
    for (size_t i = 0; ok && i < source->count; i++)
    {
        const Open32PathPoint *b = &source->points[i];
        for (unsigned k = 0; ok && k < OCTAGON_VERTICES; k++)
        {
            double angle = k * PI / 4;
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

BOOL open32PathRecording(const struct Open32DC *dc)
{
    return dc->path && dc->path->recording;
}
