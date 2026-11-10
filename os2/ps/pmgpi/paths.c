/* Public domain. No rights reserved. Wholly AI-authored. */
/* Retained paths share the ordinary PM drawing stream; raster filling uses
   scanline edge intersections, including disjoint contours and holes. */
#include "os2/include/pmgpi.h"
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/ps/pmgpi/internal.h"
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

/* Bounds on the host work for one figure. */
enum
{
    SPAN_INITIAL_CAPACITY = 256,
    MAX_REGION_SPANS = 1048576,
    MAX_SCAN_ROWS = 65536,
    MAX_POLYGONS = 65536,
    MAX_POLYGON_POINTS = 65536,
    /* Arcs are drawn as lines, two per degree of sweep, up to MAX_ARC_SEGMENTS. */
    MAX_ARC_SEGMENTS = 65536,
    MAX_ARC_SWEEP_DEGREES = 36000,
    /* Round line ends and joins are polygons of 12 to 64 vertices, about two per pel of radius. */
    DISC_MIN_VERTICES = 12,
    DISC_MAX_VERTICES = 64,
    /* A mitre whose point lies more than ten half-widths from its vertex is bevelled. */
    MITRE_LIMIT = 10
};

static const unsigned AREA_PRIMITIVE = GPI_PRIM_INDEX(PRIM_AREA);

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
    if (!p || p->record_path || (options & ~(BA_BOUNDARY | BA_WINDING | BA_EXCL)))
        return FALSE;
    if (!begin_path(p, AREA_PATH))
        return FALSE;
    p->area_recording = TRUE;
    p->area_options = options;
    return TRUE;
}

typedef struct PathSpans
{
    HPS ps;
    BOOL region;
    unsigned primitive;
    RECTL *rects;
    size_t count, capacity;
} PathSpans;

static BOOL path_span(void *context, LONG y, LONG left, LONG right)
{
    PathSpans *spans = context;
    if (!spans->region)
        return lx_pm_fill_span(spans->ps, left, y, right - left, spans->primitive);
    if (spans->count == spans->capacity)
    {
        size_t n = spans->capacity ? spans->capacity * 2 : SPAN_INITIAL_CAPACITY;
        if (n > MAX_REGION_SPANS)
            return FALSE;
        RECTL *r = realloc(spans->rects, n * sizeof *r);
        if (!r)
            return FALSE;
        spans->rects = r;
        spans->capacity = n;
    }
    spans->rects[spans->count++] = (RECTL){left, y, right, y + 1};
    return TRUE;
}

/* Fills (or converts to a region) the path's figures as bound in device space. */
static LONG raster_path(HPS h, PmPath *path, LONG options, BOOL region, unsigned primitive)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || !path || p->record_path || (options & ~(FPATH_WINDING | FPATH_EXCL)))
        return GPI_ERROR;
    if (!path->count)
        return region ? (LONG)lx_pm_region_create(h, 0, NULL) : GPI_OK;
    PmScanPoint *points = malloc(path->count * sizeof *points);
    size_t *counts = malloc(path->count * sizeof *counts), contours = 0;
    if (!points || !counts)
    {
        free(points);
        free(counts);
        return GPI_ERROR;
    }
    LONG bottom = INT32_MAX, top = INT32_MIN;
    for (size_t i = 0; i < path->count; i++)
    {
        POINTL device = path->points[i].device;
        points[i] = (PmScanPoint){device.x, device.y};
        if (!contours || path->points[i].move)
            counts[contours++] = 0;
        counts[contours - 1]++;
        if (device.y < bottom)
            bottom = device.y;
        if (device.y > top)
            top = device.y;
    }
    PathSpans spans = {h, region, primitive, NULL, 0, 0};
    unsigned rule = (options & FPATH_WINDING ? LX_PM_SCAN_WINDING : LX_PM_SCAN_ALTERNATE) |
                    (options & FPATH_EXCL ? 0 : LX_PM_SCAN_INCLUSIVE);
    BOOL ok = (int64_t)top - bottom <= MAX_SCAN_ROWS && top != INT32_MAX &&
              lx_pm_scan_polygons(points, counts, contours, rule, INT32_MIN, INT32_MAX, path_span,
                                  &spans);
    LONG result =
        ok ? (region ? (LONG)lx_pm_region_create(h, spans.count, spans.rects) : lx_pm_hits(p))
           : GPI_ERROR;
    free(spans.rects);
    free(points);
    free(counts);
    return result;
}

LONG GpiFillPath(HPS h, LONG id, LONG options)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || id != PUBLIC_PATH)
        return GPI_ERROR;
    LONG result = raster_path(h, find_path(p, id), options, FALSE, AREA_PRIMITIVE);
    if (result)
        delete_path(p, id);
    return result;
}

ULONG GpiPathToRegion(HPS h, LONG id, LONG options)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || id != PUBLIC_PATH)
        return 0;
    ULONG result = (ULONG)raster_path(h, find_path(p, id), options, TRUE, AREA_PRIMITIVE);
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
        return GPI_ERROR;
    POINTL saved = p->position, first = {0, 0};
    LONG result = GPI_OK;
    BOOL open = FALSE;
    for (size_t i = 0; i <= path->count && result; i++)
    {
        BOOL ending = i == path->count || path->points[i].move;
        if (ending && open && close && (p->position.x != first.x || p->position.y != first.y))
        {
            LONG drawn = lx_pm_line(h, &first, 0);
            result = !drawn ? GPI_ERROR : drawn == GPI_HITS ? GPI_HITS : result;
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
            result = !drawn ? GPI_ERROR : drawn == GPI_HITS ? GPI_HITS : result;
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
    StrokePoint v[DISC_MAX_VERTICES];
    unsigned count = (unsigned)ceil(radius * 2);
    if (count < DISC_MIN_VERTICES)
        count = DISC_MIN_VERTICES;
    if (count > DISC_MAX_VERTICES)
        count = DISC_MAX_VERTICES;
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
        return GPI_ERROR;
    LONG matrix[GPI_MATRIX_ELEMENTS];
    if (!GpiQueryModelTransformMatrix(h, GPI_MATRIX_ELEMENTS, matrix))
        return GPI_ERROR;
    /* A width under about one device pel is the thinnest possible line. The stroke
       still fills with area attributes, so the cosmetic pass borrows them. */
    POINTL from = {0, 0}, across = {p->gpi.line_width, 0}, up = {0, p->gpi.line_width};
    if (!lx_pm_transform_point(p, &from, TRUE) || !lx_pm_transform_point(p, &across, TRUE) ||
        !lx_pm_transform_point(p, &up, TRUE))
        return GPI_ERROR;
    double device_width = fmax(hypot((double)across.x - from.x, (double)across.y - from.y),
                               hypot((double)up.x - from.x, (double)up.y - from.y));
    if (device_width < 1.5)
    {
        const unsigned line = GPI_PRIM_INDEX(PRIM_LINE);
        LONG color = p->primitive_color[line], mix = p->primitive_mix[line], type = p->line_type;
        p->primitive_color[line] = p->primitive_color[AREA_PRIMITIVE];
        p->primitive_mix[line] = p->primitive_mix[AREA_PRIMITIVE];
        p->line_type = LINETYPE_SOLID;
        LONG result = trace_path(h, p, path, FALSE);
        p->primitive_color[line] = color;
        p->primitive_mix[line] = mix;
        p->line_type = type;
        if (result)
            delete_path(p, id);
        return result;
    }
    if (!path_to_world(p, path))
        return GPI_ERROR;
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
            double before = !closed && i == start && p->gpi.line_end == LINEEND_SQUARE ? radius : 0,
                   after =
                       !closed && i + 2 == end && p->gpi.line_end == LINEEND_SQUARE ? radius : 0;
            StrokePoint q[4] = {{a.x - dx * before + nx, a.y - dy * before + ny},
                                {a.x - dx * before - nx, a.y - dy * before - ny},
                                {b.x + dx * after - nx, b.y + dy * after - ny},
                                {b.x + dx * after + nx, b.y + dy * after + ny}};
            ok = stroke_polygon(p, q, 4);
        }
        if (ok && !closed && p->gpi.line_end == LINEEND_ROUND)
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
            if (p->gpi.line_join == LINEJOIN_ROUND)
            {
                ok = stroke_disc(p, v, radius);
                continue;
            }
            double side = turn > 0 ? -1 : 1;
            StrokePoint left = {v.x - side * ay * radius, v.y + side * ax * radius},
                        right = {v.x - side * by * radius, v.y + side * bx * radius},
                        center = {v.x, v.y};
            if (p->gpi.line_join == LINEJOIN_MITRE)
            {
                double t = ((right.x - left.x) * by - (right.y - left.y) * bx) / turn;
                StrokePoint mitre = {left.x + t * ax, left.y + t * ay};
                if (hypot(mitre.x - v.x, mitre.y - v.y) <= radius * MITRE_LIMIT)
                    center = mitre;
            }
            StrokePoint triangle[3] = {left, center, right};
            ok = stroke_polygon(p, triangle, 3);
        }
        start = end;
    }
    p->record_path = NULL;
    /* Rounded outline vertices already reach the envelope, so fill exclusively. */
    LONG result = ok ? raster_path(h, &outline, FPATH_WINDING | FPATH_EXCL, FALSE, AREA_PRIMITIVE)
                     : GPI_ERROR;
    free(outline.points);
    if (result)
        delete_path(p, id);
    return result;
}

LONG GpiEndArea(HPS h)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || !p->record_path || !p->area_recording)
        return GPI_ERROR;
    ULONG options = p->area_options;
    PmPath *path = p->record_path;
    p->record_path = NULL;
    p->area_recording = FALSE;
    LONG result = raster_path(h, path, options & (BA_WINDING | BA_EXCL), FALSE, AREA_PRIMITIVE);
    if (result && (options & BA_BOUNDARY))
    {
        LONG boundary = trace_path(h, p, path, TRUE);
        result = !boundary ? GPI_ERROR : boundary == GPI_HITS ? GPI_HITS : result;
    }
    delete_path(p, AREA_PATH);
    return result;
}

static LONG arc(HPS h, POINTL center, LONG multiplier, double begin, double sweep, BOOL move_first)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || multiplier < 0 || !isfinite(begin) || !isfinite(sweep) ||
        fabs(sweep) > MAX_ARC_SWEEP_DEGREES)
        return GPI_ERROR;
    LONG ap[4] = {1, 1, 0, 0}, result = GPI_OK;
    if (p->gpi.initialized)
        memcpy(ap, p->gpi.arc, sizeof ap);
    double factor = multiplier / (double)GPI_FIXED_ONE;
    unsigned steps = (unsigned)ceil(fabs(sweep) * 2) + 1;
    if (steps > MAX_ARC_SEGMENTS)
        steps = MAX_ARC_SEGMENTS;
    for (unsigned i = 0; i <= steps; i++)
    {
        double angle = (begin + sweep * i / steps) * 3.14159265358979323846 / 180.0;
        double x = center.x + factor * (ap[0] * cos(angle) + ap[2] * sin(angle)),
               y = center.y + factor * (ap[3] * cos(angle) + ap[1] * sin(angle));
        if (!isfinite(x) || !isfinite(y) || x < INT32_MIN || x > INT32_MAX || y < INT32_MIN ||
            y > INT32_MAX)
            return GPI_ERROR;
        POINTL v = {(LONG)llround(x), (LONG)llround(y)};
        if (!i && move_first)
        {
            if (!lx_pm_move(h, &v))
                return GPI_ERROR;
        }
        else
        {
            LONG drawn = lx_pm_line(h, &v, 0);
            if (!drawn)
                return GPI_ERROR;
            if (drawn == GPI_HITS)
                result = GPI_HITS;
        }
    }
    return result;
}

LONG GpiPartialArc(HPS h, PPOINTL center, LONG multiplier, LONG start, LONG sweep)
{
    return center ? arc(h, *center, multiplier, start / (double)GPI_FIXED_ONE,
                        sweep / (double)GPI_FIXED_ONE, FALSE)
                  : GPI_ERROR;
}

LONG GpiFullArc(HPS h, LONG control, LONG multiplier)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || control < DRO_FILL || control > DRO_OUTLINEFILL)
        return GPI_ERROR;
    POINTL saved = p->position;
    if (control & DRO_FILL)
    {
        if (p->record_path)
            return GPI_ERROR;
        if (!GpiBeginArea(h, (control & DRO_OUTLINE) ? BA_BOUNDARY : BA_NOBOUNDARY))
            return GPI_ERROR;
    }
    LONG result = arc(h, saved, multiplier, 0, 360, TRUE);
    if (control & DRO_FILL)
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
        return GPI_ERROR;
    LONG ap[4] = {1, 1, 0, 0};
    if (p->gpi.initialized)
        memcpy(ap, p->gpi.arc, sizeof ap);
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
        return GPI_ERROR;
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
    unsigned n = required > MAX_ARC_SEGMENTS ? MAX_ARC_SEGMENTS : (unsigned)required;
    LONG result = GPI_OK;
    for (unsigned i = 1; i <= n; i++)
    {
        double t = t0 + (t2 - t0) * i / n, ux = x + r * cos(t), uy = y + r * sin(t);
        double qx = ap[0] * ux + ap[2] * uy, qy = ap[3] * ux + ap[1] * uy;
        if (i < n && (qx < INT32_MIN || qx > INT32_MAX || qy < INT32_MIN || qy > INT32_MAX))
            return GPI_ERROR;
        POINTL q = i == n ? end : (POINTL){(LONG)llround(qx), (LONG)llround(qy)};
        LONG drawn = lx_pm_line(h, &q, 0);
        if (!drawn)
            return GPI_ERROR;
        if (drawn == GPI_HITS)
            result = GPI_HITS;
    }
    return result;
}

LONG GpiPolygons(HPS h, ULONG count, LXPOLYGON *polygons, ULONG options, ULONG model)
{
    PresentationSpace *p = lx_pm_font_space(h);
    if (!p || p->record_path || !polygons || count > MAX_POLYGONS ||
        (options & ~(POLYGON_BOUNDARY | POLYGON_WINDING | POLYGON_EXCL | POLYGON_NOFILL)) ||
        (model != POLYGON_INCL && model != POLYGON_EXCL))
        return GPI_ERROR;
    POINTL saved = p->position;
    if (!begin_path(p, POLYGON_PATH))
        return GPI_ERROR;
    PmPath *path = p->record_path;
    BOOL ok = TRUE;
    for (ULONG i = 0; i < count && ok; i++)
    {
        LXPOLYGON *x = &polygons[i];
        if (x->count > MAX_POLYGON_POINTS || (x->count && !x->points))
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
    LONG result = ok ? GPI_OK : GPI_ERROR;
    /* The fill rule comes from the options; inclusion from the model. */
    if (ok && !(options & POLYGON_NOFILL))
        result = raster_path(h, path, (options & POLYGON_WINDING) | model, FALSE, AREA_PRIMITIVE);
    if (result && (options & POLYGON_BOUNDARY))
    {
        LONG boundary = trace_path(h, p, path, TRUE);
        result = !boundary ? GPI_ERROR : boundary == GPI_HITS ? GPI_HITS : result;
    }
    delete_path(p, POLYGON_PATH);
    p->position = saved;
    return result;
}
