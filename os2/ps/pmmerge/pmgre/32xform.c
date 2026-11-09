/* Public domain. No rights reserved. Wholly AI-authored. */
/* Page units and an explicit viewport map model/view coordinates to device pels. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmgre/graphics.h"
#include <math.h>

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

static BOOL matrix_point(const LONG *m, POINTL *p, BOOL inverse)
{
    double a = m[0] / 65536.0, b = m[1] / 65536.0, c = m[3] / 65536.0, d = m[4] / 65536.0;
    double x = p->x, y = p->y, tx = m[6], ty = m[7], w = m[8];
    if (m[2] || m[5] || w != 1)
        return FALSE;
    if (inverse)
    {
        double det = a * d - b * c;
        if (fabs(det) < 1e-15)
            return FALSE;
        x -= tx;
        y -= ty;
        double old = x;
        x = (d * x - c * y) / det;
        y = (-b * old + a * y) / det;
    }
    else
    {
        double old = x;
        x = a * x + c * y + tx;
        y = b * old + d * y + ty;
    }
    if (!isfinite(x) || !isfinite(y) || x < INT32_MIN || x > INT32_MAX || y < INT32_MIN ||
        y > INT32_MAX)
        return FALSE;
    p->x = (LONG)llround(x);
    p->y = (LONG)llround(y);
    return TRUE;
}

BOOL lx_pm_transform_point(PresentationSpace *ps, POINTL *point, BOOL to_device)
{
    if (!ps || !point)
        return FALSE;
    POINTL original = *point;
    if (to_device && ps->gpi.initialized &&
        (!matrix_point(ps->gpi.model, point, FALSE) || !matrix_point(ps->gpi.view, point, FALSE)))
    {
        *point = original;
        return FALSE;
    }
    if (!ps->viewport_set)
    {
        LONG pels, units;
        unit_scale(ps, &pels, &units);
        POINTL output;
        if (!convert_coordinate(point->x, 0, pels, units, to_device, &output.x) ||
            !convert_coordinate(point->y, 0, pels, units, to_device, &output.y))
            return FALSE;
        if (!to_device && ps->gpi.initialized &&
            (!matrix_point(ps->gpi.view, &output, TRUE) ||
             !matrix_point(ps->gpi.model, &output, TRUE)))
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
    if (!to_device && ps->gpi.initialized &&
        (!matrix_point(ps->gpi.view, &output, TRUE) || !matrix_point(ps->gpi.model, &output, TRUE)))
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
    POINTL *converted = count ? malloc((size_t)count * sizeof *converted) : NULL;
    if (count && !converted)
        return FALSE;
    BOOL ok = TRUE;
    for (LONG i = 0; i < count && ok; i++)
    {
        POINTL p = points[i];
        /* Device/page conversion uses the common viewport mapper with matrices temporarily disabled. */
        BOOL enabled = ps->gpi.initialized;
        if (source == 5)
        {
            ps->gpi.initialized = FALSE;
            ok = lx_pm_transform_point(ps, &p, FALSE);
            ps->gpi.initialized = enabled;
        }
        if (ok && enabled && source >= 3 && target <= 2)
            ok = matrix_point(ps->gpi.view, &p, TRUE);
        if (ok && enabled && source >= 2 && target == 1)
            ok = matrix_point(ps->gpi.model, &p, TRUE);
        if (ok && enabled && source == 1 && target >= 2)
            ok = matrix_point(ps->gpi.model, &p, FALSE);
        if (ok && enabled && source <= 2 && target >= 3)
            ok = matrix_point(ps->gpi.view, &p, FALSE);
        if (ok && target == 5)
        {
            ps->gpi.initialized = FALSE;
            ok = lx_pm_transform_point(ps, &p, TRUE);
            ps->gpi.initialized = enabled;
        }
        converted[i] = p;
    }
    if (ok && count)
        memcpy(points, converted, (size_t)count * sizeof *points);
    free(converted);
    return ok;
}

/* Page coordinates exclude the model and viewing transforms. */
static BOOL page_point(PresentationSpace *ps, POINTL *point, BOOL to_device)
{
    BOOL enabled = ps->gpi.initialized;
    ps->gpi.initialized = FALSE;
    BOOL ok = lx_pm_transform_point(ps, point, to_device);
    ps->gpi.initialized = enabled;
    return ok;
}

/* Bounding device box of a page or model rectangle. The limits include their top and
   right edges, so the result is widened to exclusive RECTL form. */
static BOOL device_box(PresentationSpace *ps, const RECTL *r, BOOL page, RECTL *output)
{
    POINTL corners[4] = {
        {r->xLeft, r->yBottom}, {r->xRight, r->yBottom}, {r->xRight, r->yTop}, {r->xLeft, r->yTop}};
    for (unsigned i = 0; i < 4; i++)
        if (!(page ? page_point(ps, &corners[i], TRUE)
                   : lx_pm_transform_point(ps, &corners[i], TRUE)))
            return FALSE;
    RECTL box = {corners[0].x, corners[0].y, corners[0].x, corners[0].y};
    for (unsigned i = 1; i < 4; i++)
    {
        if (corners[i].x < box.xLeft)
            box.xLeft = corners[i].x;
        if (corners[i].x > box.xRight)
            box.xRight = corners[i].x;
        if (corners[i].y < box.yBottom)
            box.yBottom = corners[i].y;
        if (corners[i].y > box.yTop)
            box.yTop = corners[i].y;
    }
    if (box.xRight == INT32_MAX || box.yTop == INT32_MAX)
        return FALSE;
    box.xRight++;
    box.yTop++;
    *output = box;
    return TRUE;
}

BOOL lx_pm_graphics_clip(PresentationSpace *ps, LONG x, LONG y)
{
    if (!ps || !ps->gpi.initialized)
        return TRUE;
    if (!ps->gpi.display_enabled)
        return FALSE;
    /* Both limits become device rectangles; any rotation or shear of them is ignored. */
    RECTL limit = {INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX};
    lx_pm_graphics_limits(ps, &limit);
    return x >= limit.xLeft && x < limit.xRight && y >= limit.yBottom && y < limit.yTop;
}

void lx_pm_graphics_limits(PresentationSpace *ps, RECTL *rect)
{
    if (!ps || !ps->gpi.initialized)
        return;
    if (ps->gpi.graphics_set)
    {
        RECTL device;
        if (device_box(ps, &ps->gpi.graphics_field, TRUE, &device))
            WinIntersectRect(0, rect, rect, &device);
        else
            *rect = (RECTL){0, 0, 0, 0};
    }
    if (ps->gpi.viewing_set)
    {
        RECTL device;
        if (device_box(ps, &ps->gpi.viewing_limits, FALSE, &device))
            WinIntersectRect(0, rect, rect, &device);
        else
            *rect = (RECTL){0, 0, 0, 0};
    }
}

/* The aperture is centred on its position in page coordinates. */
static BOOL pick_box(PresentationSpace *ps, RECTL *box)
{
    if (!ps->gpi.correlate)
        return FALSE;
    POINTL low = {ps->gpi.pick_position.x - ps->gpi.pick_size.cx / 2,
                  ps->gpi.pick_position.y - ps->gpi.pick_size.cy / 2};
    POINTL high = {low.x + ps->gpi.pick_size.cx, low.y + ps->gpi.pick_size.cy};
    if (!page_point(ps, &low, TRUE) || !page_point(ps, &high, TRUE))
        return FALSE;
    *box = (RECTL){low.x < high.x ? low.x : high.x, low.y < high.y ? low.y : high.y,
                   low.x < high.x ? high.x : low.x, low.y < high.y ? high.y : low.y};
    return TRUE;
}

/* Clips the segment to the aperture, so a diagonal near its corner is not a hit. */
void lx_pm_pick_line(PresentationSpace *ps, const POINTL *from, const POINTL *to)
{
    RECTL box;
    if (!ps || !pick_box(ps, &box))
        return;
    double dx = (double)to->x - from->x, dy = (double)to->y - from->y, enter = 0, leave = 1;
    double p[4] = {-dx, dx, -dy, dy};
    double q[4] = {(double)from->x - box.xLeft, (double)box.xRight - from->x,
                   (double)from->y - box.yBottom, (double)box.yTop - from->y};
    for (unsigned i = 0; i < 4; i++)
    {
        if (p[i] == 0)
        {
            if (q[i] < 0)
                return;
            continue;
        }
        double t = q[i] / p[i];
        if (p[i] < 0 && t > enter)
            enter = t;
        if (p[i] > 0 && t < leave)
            leave = t;
    }
    if (enter <= leave)
        ps->gpi.pick_hit = TRUE;
}

LONG lx_pm_hits(PresentationSpace *ps)
{
    if (!ps || !ps->gpi.pick_hit)
        return 1;
    ps->gpi.pick_hit = FALSE;
    return 2;
}

void lx_pm_boundary(PresentationSpace *ps, LONG x, LONG y, LONG width, LONG height)
{
    if (!ps || !ps->gpi.initialized || width <= 0 || height <= 0)
        return;
    RECTL r = {x, y, x + width, y + height};
    RECTL aperture;
    if (pick_box(ps, &aperture) && r.xLeft <= aperture.xRight && r.xRight > aperture.xLeft &&
        r.yBottom <= aperture.yTop && r.yTop > aperture.yBottom)
        ps->gpi.pick_hit = TRUE;
    if (!ps->gpi.boundary_enabled)
        return;
    if (!ps->gpi.boundary_set)
        ps->gpi.boundary = r;
    else
        WinUnionRect(0, &ps->gpi.boundary, &ps->gpi.boundary, &r);
    ps->gpi.boundary_set = TRUE;
}
