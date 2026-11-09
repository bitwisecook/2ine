/* Public domain. No rights reserved. Wholly AI-authored. */
/* Mapping modes, window and viewport origins and extents, and the world transform's effect on
   device points. */
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/dc.h"
#include <math.h>

LONG GetMapMode(ULONG dc)
{
    Open32DC *context = open32FindDC(dc);
    if (!context)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    return context->map_mode;
}

LONG SetMapMode(ULONG dc, LONG mode)
{
    Open32DC *context = open32FindDC(dc);
    if (!context)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (mode < 1 || mode > 8)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LONG previous = context->map_mode;
    context->map_mode = mode;
    open32MetafileWords(dc, 0x103, 1, &mode);
    if (mode == 1)
        context->window_width = context->window_height = context->viewport_width =
            context->viewport_height = 1;
    else if (mode < 7)
    {
        /* Metric, English and twip modes are logical units per inch with y increasing upward. */
        static const LONG units[] = {254, 2540, 100, 1000, 1440};
        context->window_width = context->window_height = units[mode - 2];
        context->viewport_width = GetDeviceCaps(dc, 88);
        context->viewport_height = -GetDeviceCaps(dc, 90);
    }
    return previous;
}

BOOL open32IdentityMapping(Open32DC *dc)
{
    return dc && !dc->world_active && !dc->fit_active && dc->window_x == dc->viewport_x &&
           dc->window_y == dc->viewport_y && dc->window_width == dc->viewport_width &&
           dc->window_height == dc->viewport_height;
}

static BOOL map_axis(LONG *value, LONG from, LONG to, LONG numerator, LONG denominator)
{
    int64_t product = ((int64_t)*value - from) * numerator;
    int64_t result = product / denominator + to;
    if (result < INT32_MIN || result > INT32_MAX)
        return FALSE;
    *value = (LONG)result;
    return TRUE;
}

/* Scale and translation keep rectangles axis aligned; other world transforms do not. */
BOOL open32Rotated(const Open32DC *dc)
{
    return dc->world_active && (dc->world[1] != 0 || dc->world[2] != 0);
}

static void apply_matrix(const double m[6], double *x, double *y)
{
    double a = *x, b = *y;
    *x = a * m[0] + b * m[2] + m[4];
    *y = a * m[1] + b * m[3] + m[5];
}

static BOOL invert_matrix(const double m[6], double *x, double *y)
{
    double determinant = m[0] * m[3] - m[1] * m[2], a = *x - m[4], b = *y - m[5];
    if (determinant == 0)
        return FALSE;
    *x = (a * m[3] - b * m[2]) / determinant;
    *y = (b * m[0] - a * m[1]) / determinant;
    return TRUE;
}

static BOOL map_transformed(Open32DC *dc, LONG *x, LONG *y, BOOL device)
{
    double a = *x, b = *y;
    double sx = (double)dc->viewport_width / dc->window_width,
           sy = (double)dc->viewport_height / dc->window_height;
    if (device)
    {
        if (dc->world_active)
            apply_matrix(dc->world, &a, &b);
        a = (a - dc->window_x) * sx + dc->viewport_x;
        b = (b - dc->window_y) * sy + dc->viewport_y;
        if (dc->fit_active)
            apply_matrix(dc->fit, &a, &b);
    }
    else
    {
        if (dc->fit_active && !invert_matrix(dc->fit, &a, &b))
            return FALSE;
        a = (a - dc->viewport_x) / sx + dc->window_x;
        b = (b - dc->viewport_y) / sy + dc->window_y;
        if (dc->world_active && !invert_matrix(dc->world, &a, &b))
            return FALSE;
    }
    if (fabs(a) > INT32_MAX || fabs(b) > INT32_MAX)
        return FALSE;
    *x = (LONG)lround(a);
    *y = (LONG)lround(b);
    return TRUE;
}

BOOL open32MapPoint(Open32DC *dc, LONG *x, LONG *y, BOOL device)
{
    if (dc->world_active || dc->fit_active)
    {
        if (map_transformed(dc, x, y, device))
            return TRUE;
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LONG a = *x, b = *y;
    BOOL valid =
        device
            ? map_axis(&a, dc->window_x, dc->viewport_x, dc->viewport_width, dc->window_width) &&
                  map_axis(&b, dc->window_y, dc->viewport_y, dc->viewport_height, dc->window_height)
            : map_axis(&a, dc->viewport_x, dc->window_x, dc->window_width, dc->viewport_width) &&
                  map_axis(&b, dc->viewport_y, dc->window_y, dc->window_height,
                           dc->viewport_height);
    if (!valid)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *x = a;
    *y = b;
    return TRUE;
}

static BOOL map_points(ULONG handle, PVOID points, LONG count, BOOL device)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || count < 0 || count > 65536 || (count && !points))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (LONG i = 0; i < count; i++)
    {
        uint8 *wire = (uint8 *)points + i * 8;
        LONG x = (LONG)lx_guest_load_u32(wire), y = (LONG)lx_guest_load_u32(wire + 4);
        if (!open32MapPoint(dc, &x, &y, device))
            return FALSE;
        lx_guest_store_u32(wire, (ULONG)x);
        lx_guest_store_u32(wire + 4, (ULONG)y);
    }
    return TRUE;
}

BOOL DPtoLP(ULONG handle, PVOID points, LONG count)
{
    return map_points(handle, points, count, FALSE);
}

BOOL LPtoDP(ULONG handle, PVOID points, LONG count)
{
    return map_points(handle, points, count, TRUE);
}

/* MM_ISOTROPIC shrinks one viewport extent so both axes share the window's aspect ratio. */
static void isotropic_viewport(Open32DC *dc)
{
    double width = fabs((double)dc->viewport_width), height = fabs((double)dc->viewport_height);
    double aspect = fabs((double)dc->window_width / dc->window_height);
    if (height * aspect < width)
        dc->viewport_width =
            (LONG)lround(dc->viewport_width < 0 ? -height * aspect : height * aspect);
    else
        dc->viewport_height =
            (LONG)lround(dc->viewport_height < 0 ? -width / aspect : width / aspect);
    if (!dc->viewport_width)
        dc->viewport_width = 1;
    if (!dc->viewport_height)
        dc->viewport_height = 1;
}

static BOOL set_mapping(ULONG handle, LONG x, LONG y, PVOID previous, unsigned kind)
{
    TRACE_NATIVE("SetMapping(%u, %d, %d, %p, %u)", handle, x, y, previous, kind);
    Open32DC *dc = open32FindDC(handle);
    if (!dc)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    LONG *first = kind == 0   ? &dc->viewport_x
                  : kind == 1 ? &dc->window_x
                  : kind == 2 ? &dc->viewport_width
                              : &dc->window_width;
    LONG *second = kind == 0   ? &dc->viewport_y
                   : kind == 1 ? &dc->window_y
                   : kind == 2 ? &dc->viewport_height
                               : &dc->window_height;
    if (kind >= 2 && dc->map_mode >= 7 && (!x || !y))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (previous)
    {
        lx_guest_store_u32(previous, (ULONG)*first);
        lx_guest_store_u32((uint8 *)previous + 4, (ULONG)*second);
    }
    if (kind < 2 || dc->map_mode >= 7)
    {
        *first = x;
        *second = y;
    }
    if (kind >= 2 && dc->map_mode == 7)
        isotropic_viewport(dc);
    const LONG values[] = {x, y};
    open32MetafileWords(handle,
                        kind == 0   ? 0x20d
                        : kind == 1 ? 0x20b
                        : kind == 2 ? 0x20e
                                    : 0x20c,
                        2, values);
    return TRUE;
}

BOOL SetViewportOrgEx(ULONG dc, LONG x, LONG y, PVOID previous)
{
    return set_mapping(dc, x, y, previous, 0);
}

BOOL SetWindowOrgEx(ULONG dc, LONG x, LONG y, PVOID previous)
{
    return set_mapping(dc, x, y, previous, 1);
}

BOOL SetViewportExtEx(ULONG dc, LONG x, LONG y, PVOID previous)
{
    return set_mapping(dc, x, y, previous, 2);
}

BOOL SetWindowExtEx(ULONG dc, LONG x, LONG y, PVOID previous)
{
    return set_mapping(dc, x, y, previous, 3);
}

/* Mode 0 replaces the transform; MWT_IDENTITY 1 clears it; MWT_LEFTMULTIPLY 2 and
   MWT_RIGHTMULTIPLY 3 compose with the current one. */
BOOL open32WorldTransform(ULONG h, const double m[6], ULONG mode)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || mode > 3)
    {
        open32SetError(dc ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
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
