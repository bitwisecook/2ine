/* Public domain. No rights reserved. Wholly AI-authored. */
/* Retained paths share the ordinary PM drawing stream; raster filling uses
   scanline edge intersections, including disjoint contours and holes. */
#include "os2/include/pmgpi.h"
#include "os2/ps/pmwin/internal.h"
#include "os2/ps/pmwin/graphics.h"
#include <math.h>

static PmPath *find_path(PresentationSpace *p, LONG id)
{
    for (PmPath *x = p->paths; x; x = x->next)
        if (x->id == id)
            return x;
    return NULL;
}

/* Identifiers above the public path 1 name internal area and polygon brackets. */
enum
{
    PUBLIC_PATH = 1,
    AREA_PATH = INT32_MAX,
    POLYGON_PATH = INT32_MAX - 1
};

static void delete_path(PresentationSpace *p, LONG id)
{
    for (PmPath **link = &p->paths; *link; link = &(*link)->next)
        if ((*link)->id == id)
        {
            PmPath *x = *link;
            *link = x->next;
            free(x->points);
            free(x);
            return;
        }
}

static BOOL begin_path(PresentationSpace *p, LONG id)
{
    PmPath *x = find_path(p, id);
    if (!x)
    {
        x = calloc(1, sizeof *x);
        if (!x)
            return FALSE;
        x->id = id;
        x->next = p->paths;
        p->paths = x;
    }
    x->count = 0;
    p->record_path = x;
    return lx_pm_record_point(p, &p->position, TRUE);
}

BOOL GpiBeginPath(HPS h, LONG id)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || id != PUBLIC_PATH || p->record_path)
        return FALSE;
    return begin_path(p, id);
}

BOOL GpiEndPath(HPS h)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || !p->record_path || p->area_recording)
        return FALSE;
    p->record_path = NULL;
    return TRUE;
}

BOOL GpiCloseFigure(HPS h)
{
    PresentationSpace *p = lx_pm_font_space(h);
    PmPath *x = p ? p->record_path : NULL;
    if (!x || !x->count)
        return FALSE;
    size_t at = x->count - 1;
    while (at && !x->points[at].move)
        at--;
    POINTL first = x->points[at].point;
    if (!lx_pm_record_point(p, &first, FALSE))
        return FALSE;
    p->position = first;
    return TRUE;
}

BOOL GpiBeginArea(HPS h, ULONG options)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || p->record_path || (options & ~0xbu))
        return FALSE;
    if (!begin_path(p, AREA_PATH))
        return FALSE;
    p->area_recording = TRUE;
    p->area_options = options;
    return TRUE;
}

typedef struct Edge
{
    double x;
    int winding;
} Edge;

static int compare_edge(const void *a, const void *b)
{
    double x = ((const Edge *)a)->x, y = ((const Edge *)b)->x;
    return x < y ? -1 : x > y;
}

static BOOL add_span(RECTL **rects, size_t *count, size_t *capacity, LONG left, LONG right, LONG y)
{
    if (left >= right)
        return TRUE;
    if (*count == *capacity)
    {
        size_t n = *capacity ? *capacity * 2 : 256;
        if (n > 1048576)
            return FALSE;
        RECTL *r = realloc(*rects, n * sizeof *r);
        if (!r)
            return FALSE;
        *rects = r;
        *capacity = n;
    }
    (*rects)[(*count)++] = (RECTL){left, y, right, y + 1};
    return TRUE;
}

static LONG raster_path(HPS h, PmPath *path, LONG options, BOOL region, unsigned primitive)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || !path || p->record_path || (options & ~0xau))
        return region ? 0 : 0;
    if (!path->count)
        return region ? (LONG)lx_pm_region_create(h, 0, NULL) : 1;
    /* Inclusive fills also cover the top scan line and each span's right edge. */
    BOOL inclusive = !(options & 8);
    PmPathPoint *v = malloc(path->count * sizeof *v);
    Edge *edges = malloc(path->count * sizeof *edges);
    if (!v || !edges)
    {
        free(v);
        free(edges);
        return 0;
    }
    LONG bottom = INT32_MAX, top = INT32_MIN;
    BOOL ok = TRUE;
    for (size_t i = 0; i < path->count; i++)
    {
        v[i] = path->points[i];
        v[i].point = path->points[i].device;
        if (v[i].point.y < bottom)
            bottom = v[i].point.y;
        if (v[i].point.y > top)
            top = v[i].point.y;
    }
    if ((int64_t)top - bottom > 65536 || top == INT32_MAX)
        ok = FALSE;
    RECTL *rects = NULL;
    size_t count = 0, capacity = 0;
    for (LONG y = bottom; ok && y < top + (inclusive ? 1 : 0); y++)
    {
        size_t n = 0, start = 0;
        double sample = y < top ? y + 0.5 : y - 0.5;
        for (size_t i = 0; i < path->count; i++)
        {
            if (v[i].move)
                start = i;
            size_t end = (i + 1 == path->count || v[i + 1].move) ? start : i + 1;
            POINTL a = v[i].point, b = v[end].point;
            if (a.y == b.y || sample < (a.y < b.y ? a.y : b.y) || sample >= (a.y > b.y ? a.y : b.y))
                continue;
            edges[n++] = (Edge){a.x + ((sample - a.y) * (b.x - (double)a.x)) / (b.y - (double)a.y),
                                b.y > a.y ? 1 : -1};
        }
        qsort(edges, n, sizeof *edges, compare_edge);
        int winding = 0;
        double left = 0;
        for (size_t j = 0; j < n; j++)
        {
            BOOL before = (options & 2) ? winding != 0 : (winding & 1) != 0;
            winding += (options & 2) ? edges[j].winding : 1;
            BOOL after = (options & 2) ? winding != 0 : (winding & 1) != 0;
            if (!before && after)
                left = edges[j].x;
            if (before && !after)
            {
                double right = edges[j].x;
                if (left < INT32_MIN || right >= INT32_MAX)
                {
                    ok = FALSE;
                    break;
                }
                LONG l = (LONG)ceil(left - 0.5), r = (LONG)ceil(right - 0.5) + (inclusive ? 1 : 0);
                if (region)
                    ok = add_span(&rects, &count, &capacity, l, r, y);
                else if (r > l)
                    ok = lx_pm_fill_span(h, l, y, r - l, primitive);
                if (!ok)
                    break;
            }
        }
    }
    LONG result = ok ? (region ? (LONG)lx_pm_region_create(h, count, rects) : lx_pm_hits(p)) : 0;
    free(rects);
    free(v);
    free(edges);
    return result;
}

LONG GpiFillPath(HPS h, LONG id, LONG options)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || id != PUBLIC_PATH)
        return 0;
    LONG result = raster_path(h, find_path(p, id), options, FALSE, 3);
    if (result)
        delete_path(p, id);
    return result;
}

ULONG GpiPathToRegion(HPS h, LONG id, LONG options)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || id != PUBLIC_PATH)
        return 0;
    ULONG result = (ULONG)raster_path(h, find_path(p, id), options, TRUE, 3);
    if (result)
        delete_path(p, id);
    return result;
}

/* Later transforms do not move a path, so its device points are mapped back through them. */
static BOOL path_to_world(PresentationSpace *p, PmPath *path)
{
    for (size_t i = 0; i < path->count; i++)
    {
        POINTL world = path->points[i].device;
        if (!lx_pm_transform_point(p, &world, FALSE))
            return FALSE;
        path->points[i].point = world;
    }
    return TRUE;
}

/* Traces figures with the current cosmetic line, closing them when requested. */
static LONG trace_path(HPS h, PresentationSpace *p, PmPath *path, BOOL close)
{
    if (!path_to_world(p, path))
        return 0;
    POINTL saved = p->position, first = {0, 0};
    LONG result = 1;
    BOOL open = FALSE;
    for (size_t i = 0; i <= path->count && result; i++)
    {
        BOOL ending = i == path->count || path->points[i].move;
        if (ending && open && close && (p->position.x != first.x || p->position.y != first.y))
        {
            LONG drawn = lx_pm_line(h, &first, 0);
            result = !drawn ? 0 : drawn == 2 ? 2 : result;
        }
        if (i == path->count || !result)
            break;
        if (path->points[i].move)
        {
            p->position = first = path->points[i].point;
            open = TRUE;
        }
        else
        {
            LONG drawn = lx_pm_line(h, &path->points[i].point, 0);
            result = !drawn ? 0 : drawn == 2 ? 2 : result;
        }
    }
    p->position = saved;
    return result;
}

typedef struct StrokePoint
{
    double x, y;
} StrokePoint;

static BOOL stroke_polygon(PresentationSpace *p, StrokePoint *vertices, unsigned count)
{
    double area = 0;
    for (unsigned i = 0; i < count; i++)
    {
        unsigned j = (i + 1) % count;
        area += vertices[i].x * vertices[j].y - vertices[j].x * vertices[i].y;
    }
    for (unsigned i = 0; i < count; i++)
    {
        StrokePoint v = vertices[area < 0 ? count - 1 - i : i];
        if (!isfinite(v.x) || !isfinite(v.y) || v.x < INT32_MIN || v.x > INT32_MAX ||
            v.y < INT32_MIN || v.y > INT32_MAX)
            return FALSE;
        POINTL q = {(LONG)llround(v.x), (LONG)llround(v.y)};
        if (!lx_pm_record_point(p, &q, i == 0))
            return FALSE;
    }
    return TRUE;
}

static BOOL stroke_disc(PresentationSpace *p, POINTL center, double radius)
{
    StrokePoint v[64];
    unsigned count = (unsigned)ceil(radius * 2);
    if (count < 12)
        count = 12;
    if (count > 64)
        count = 64;
    for (unsigned i = 0; i < count; i++)
    {
        double a = 6.283185307179586 * i / count;
        v[i] = (StrokePoint){center.x + radius * cos(a), center.y + radius * sin(a)};
    }
    return stroke_polygon(p, v, count);
}

LONG GpiStrokePath(HPS h, LONG id, ULONG options)
{
    PresentationSpace *p = lx_pm_font_space(h);
    PmPath *path = p && id == PUBLIC_PATH ? find_path(p, id) : NULL;
    if (!path || p->record_path || options)
        return 0;
    LONG matrix[9];
    if (!GpiQueryModelTransformMatrix(h, 9, matrix))
        return 0;
    /* A width under about one device pel is the thinnest possible line. The stroke
       still fills with area attributes, so the cosmetic pass borrows them. */
    POINTL from = {0, 0}, across = {p->gpi.line_width, 0}, up = {0, p->gpi.line_width};
    if (!lx_pm_transform_point(p, &from, TRUE) || !lx_pm_transform_point(p, &across, TRUE) ||
        !lx_pm_transform_point(p, &up, TRUE))
        return 0;
    double device_width = fmax(hypot((double)across.x - from.x, (double)across.y - from.y),
                               hypot((double)up.x - from.x, (double)up.y - from.y));
    if (device_width < 1.5)
    {
        LONG color = p->primitive_color[0], mix = p->primitive_mix[0], type = p->line_type;
        p->primitive_color[0] = p->primitive_color[3];
        p->primitive_mix[0] = p->primitive_mix[3];
        p->line_type = 7;
        LONG result = trace_path(h, p, path, FALSE);
        p->primitive_color[0] = color;
        p->primitive_mix[0] = mix;
        p->line_type = type;
        if (result)
            delete_path(p, id);
        return result;
    }
    if (!path_to_world(p, path))
        return 0;
    PmPath outline = {0};
    p->record_path = &outline;
    BOOL ok = TRUE;
    double radius = p->gpi.line_width / 2.0;
    for (size_t start = 0; start < path->count && ok;)
    {
        size_t end = start + 1;
        while (end < path->count && !path->points[end].move)
            end++;
        if (end - start < 2)
        {
            start = end;
            continue;
        }
        BOOL closed = path->points[start].point.x == path->points[end - 1].point.x &&
                      path->points[start].point.y == path->points[end - 1].point.y;
        for (size_t i = start; i + 1 < end && ok; i++)
        {
            POINTL a = path->points[i].point, b = path->points[i + 1].point;
            double dx = (double)b.x - a.x, dy = (double)b.y - a.y, length = hypot(dx, dy);
            if (!length)
                continue;
            dx /= length;
            dy /= length;
            double nx = -dy * radius, ny = dx * radius;
            double before = !closed && i == start && p->gpi.line_end == 2 ? radius : 0,
                   after = !closed && i + 2 == end && p->gpi.line_end == 2 ? radius : 0;
            StrokePoint q[4] = {{a.x - dx * before + nx, a.y - dy * before + ny},
                                {a.x - dx * before - nx, a.y - dy * before - ny},
                                {b.x + dx * after - nx, b.y + dy * after - ny},
                                {b.x + dx * after + nx, b.y + dy * after + ny}};
            ok = stroke_polygon(p, q, 4);
        }
        if (ok && !closed && p->gpi.line_end == 3)
            ok = stroke_disc(p, path->points[start].point, radius) &&
                 stroke_disc(p, path->points[end - 1].point, radius);
        size_t first = closed ? start : start + 1, last = end - 1;
        for (size_t i = first; i < last && ok; i++)
        {
            POINTL v = path->points[i].point, a = path->points[i == start ? end - 2 : i - 1].point,
                   b = path->points[i + 1].point;
            double ax = (double)v.x - a.x, ay = (double)v.y - a.y, bx = (double)b.x - v.x,
                   by = (double)b.y - v.y, al = hypot(ax, ay), bl = hypot(bx, by);
            if (!al || !bl)
                continue;
            ax /= al;
            ay /= al;
            bx /= bl;
            by /= bl;
            double turn = ax * by - ay * bx;
            if (fabs(turn) < 1e-12)
                continue;
            if (p->gpi.line_join == 2)
            {
                ok = stroke_disc(p, v, radius);
                continue;
            }
            double side = turn > 0 ? -1 : 1;
            StrokePoint left = {v.x - side * ay * radius, v.y + side * ax * radius},
                        right = {v.x - side * by * radius, v.y + side * bx * radius},
                        center = {v.x, v.y};
            if (p->gpi.line_join == 3)
            {
                double t = ((right.x - left.x) * by - (right.y - left.y) * bx) / turn;
                StrokePoint mitre = {left.x + t * ax, left.y + t * ay};
                if (hypot(mitre.x - v.x, mitre.y - v.y) <= radius * 10)
                    center = mitre;
            }
            StrokePoint triangle[3] = {left, center, right};
            ok = stroke_polygon(p, triangle, 3);
        }
        start = end;
    }
    p->record_path = NULL;
    /* Rounded outline vertices already reach the envelope, so fill exclusively. */
    LONG result = ok ? raster_path(h, &outline, 2 | 8, FALSE, 3) : 0;
    free(outline.points);
    if (result)
        delete_path(p, id);
    return result;
}

LONG GpiEndArea(HPS h)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || !p->record_path || !p->area_recording)
        return 0;
    ULONG options = p->area_options;
    PmPath *path = p->record_path;
    p->record_path = NULL;
    p->area_recording = FALSE;
    LONG result = raster_path(h, path, options & 0xa, FALSE, 3);
    if (result && (options & 1))
    {
        LONG boundary = trace_path(h, p, path, TRUE);
        result = !boundary ? 0 : boundary == 2 ? 2 : result;
    }
    delete_path(p, AREA_PATH);
    return result;
}

static LONG arc(HPS h, POINTL center, LONG multiplier, double begin, double sweep, BOOL move_first)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || multiplier < 0 || !isfinite(begin) || !isfinite(sweep) || fabs(sweep) > 36000)
        return 0;
    LONG ap[4] = {1, 1, 0, 0}, result = 1;
    if (p->gpi.initialized)
        memcpy(ap, p->gpi.arc, 16);
    double factor = multiplier / 65536.0;
    unsigned steps = (unsigned)ceil(fabs(sweep) * 2) + 1;
    if (steps > 65536)
        steps = 65536;
    for (unsigned i = 0; i <= steps; i++)
    {
        double angle = (begin + sweep * i / steps) * 3.14159265358979323846 / 180.0;
        double x = center.x + factor * (ap[0] * cos(angle) + ap[2] * sin(angle)),
               y = center.y + factor * (ap[3] * cos(angle) + ap[1] * sin(angle));
        if (!isfinite(x) || !isfinite(y) || x < INT32_MIN || x > INT32_MAX || y < INT32_MIN ||
            y > INT32_MAX)
            return 0;
        POINTL v = {(LONG)llround(x), (LONG)llround(y)};
        if (!i && move_first)
        {
            if (!lx_pm_move(h, &v))
                return 0;
        }
        else
        {
            LONG drawn = lx_pm_line(h, &v, 0);
            if (!drawn)
                return 0;
            if (drawn == 2)
                result = 2;
        }
    }
    return result;
}

LONG GpiPartialArc(HPS h, PPOINTL center, LONG multiplier, LONG start, LONG sweep)
{
    return center ? arc(h, *center, multiplier, start / 65536.0, sweep / 65536.0, FALSE) : 0;
}

LONG GpiFullArc(HPS h, LONG control, LONG multiplier)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || control < 1 || control > 3)
        return 0;
    POINTL saved = p->position;
    if (control & 1)
    {
        if (p->record_path)
            return 0;
        if (!GpiBeginArea(h, (control & 2) ? 1 : 0))
            return 0;
    }
    LONG result = arc(h, saved, multiplier, 0, 360, TRUE);
    if (control & 1)
    {
        if (result)
            result = GpiEndArea(h);
        else
        {
            p->record_path = NULL;
            p->area_recording = FALSE;
            delete_path(p, AREA_PATH);
        }
    }
    p->position = saved;
    return result;
}

static void to_unit(const LONG *ap, double det, POINTL v, double *x, double *y)
{
    *x = (ap[1] * (double)v.x - ap[2] * (double)v.y) / det;
    *y = (ap[0] * (double)v.y - ap[3] * (double)v.x) / det;
}

LONG GpiPointArc(HPS h, PPOINTL points)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || !points)
        return 0;
    LONG ap[4] = {1, 1, 0, 0};
    if (p->gpi.initialized)
        memcpy(ap, p->gpi.arc, 16);
    /* The arc is a circle in the space the arc parameters map onto the page. */
    double det = (double)ap[0] * ap[1] - (double)ap[2] * ap[3];
    POINTL end = points[1];
    if (fabs(det) < 1e-12)
        return lx_pm_line(h, &end, 0);
    double ax, ay, bx, by, cx, cy;
    to_unit(ap, det, p->position, &ax, &ay);
    to_unit(ap, det, points[0], &bx, &by);
    to_unit(ap, det, end, &cx, &cy);
    double d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (fabs(d) < 1e-9)
        return lx_pm_line(h, &end, 0);
    double aa = ax * ax + ay * ay, bb = bx * bx + by * by, cc = cx * cx + cy * cy;
    double x = (aa * (by - cy) + bb * (cy - ay) + cc * (ay - by)) / d,
           y = (aa * (cx - bx) + bb * (ax - cx) + cc * (bx - ax)) / d, r = hypot(ax - x, ay - y);
    if (!isfinite(x) || !isfinite(y) || !isfinite(r) || r > INT32_MAX)
        return 0;
    double t0 = atan2(ay - y, ax - x), t1 = atan2(by - y, bx - x), t2 = atan2(cy - y, cx - x),
           twopi = 6.283185307179586;
    while (t1 < t0)
        t1 += twopi;
    while (t2 < t0)
        t2 += twopi;
    if (t1 > t2)
        t2 -= twopi;
    double scale =
        fmax(fabs((double)ap[0]) + fabs((double)ap[2]), fabs((double)ap[3]) + fabs((double)ap[1]));
    double required = ceil(fabs(t2 - t0) * r * scale / 2) + 1;
    unsigned n = required > 65536 ? 65536 : (unsigned)required;
    LONG result = 1;
    for (unsigned i = 1; i <= n; i++)
    {
        double t = t0 + (t2 - t0) * i / n, ux = x + r * cos(t), uy = y + r * sin(t);
        double qx = ap[0] * ux + ap[2] * uy, qy = ap[3] * ux + ap[1] * uy;
        if (i < n && (qx < INT32_MIN || qx > INT32_MAX || qy < INT32_MIN || qy > INT32_MAX))
            return 0;
        POINTL q = i == n ? end : (POINTL){(LONG)llround(qx), (LONG)llround(qy)};
        LONG drawn = lx_pm_line(h, &q, 0);
        if (!drawn)
            return 0;
        if (drawn == 2)
            result = 2;
    }
    return result;
}

LONG GpiPolygons(HPS h, ULONG count, LXPOLYGON *polygons, ULONG options, ULONG model)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || p->record_path || !polygons || count > 65536 || (options & ~0x1b) ||
        (model != 0 && model != 8))
        return 0;
    POINTL saved = p->position;
    if (!begin_path(p, POLYGON_PATH))
        return 0;
    PmPath *path = p->record_path;
    BOOL ok = TRUE;
    for (ULONG i = 0; i < count && ok; i++)
    {
        LXPOLYGON *x = &polygons[i];
        if (x->count > 65536 || (x->count && !x->points))
        {
            ok = FALSE;
            break;
        }
        /* The first polygon begins at the current position; later ones list every vertex. */
        for (ULONG j = 0; j < x->count && ok; j++)
            ok = i && !j ? lx_pm_move(h, &x->points[j]) : lx_pm_line(h, &x->points[j], 0) != 0;
        if (x->count && ok)
            ok = GpiCloseFigure(h);
    }
    p->record_path = NULL;
    LONG result = ok ? 1 : 0;
    if (ok && !(options & 16))
        result = raster_path(h, path, (options & 2) | model, FALSE, 3);
    if (result && (options & 1))
    {
        LONG boundary = trace_path(h, p, path, TRUE);
        result = !boundary ? 0 : boundary == 2 ? 2 : result;
    }
    delete_path(p, POLYGON_PATH);
    p->position = saved;
    return result;
}
