/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 DC state follows PMWINX's validated-handle, previous-value convention. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/private/pmwinp.h"
#include <math.h>

/* The coordinate pairs state_pair reads. */
enum
{
    PAIR_POSITION,
    PAIR_VIEWPORT_ORIGIN,
    PAIR_WINDOW_ORIGIN,
    PAIR_VIEWPORT_EXTENT,
    PAIR_WINDOW_EXTENT,
    /* GetBkColor reports the RGB part of the COLORREF. */
    COLORREF_RGB_MASK = 0xFFFFFF
};

static BOOL state_pair(ULONG handle, PVOID output, unsigned kind)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !output)
    {
        open32SetError(dc ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
        return FALSE;
    }
    LONG x, y;
    switch (kind)
    {
        case PAIR_POSITION:
            x = dc->position_x;
            y = dc->position_y;
            break;
        case PAIR_VIEWPORT_ORIGIN:
            x = dc->viewport_x;
            y = dc->viewport_y;
            break;
        case PAIR_WINDOW_ORIGIN:
            x = dc->window_x;
            y = dc->window_y;
            break;
        case PAIR_VIEWPORT_EXTENT:
            x = dc->viewport_width;
            y = dc->viewport_height;
            break;
        default:
            x = dc->window_width;
            y = dc->window_height;
            break;
    }
    lx_guest_store_u32((uint8 *)output + WIN32_POINT_X, (ULONG)x);
    lx_guest_store_u32((uint8 *)output + WIN32_POINT_Y, (ULONG)y);
    return TRUE;
}

BOOL GetCurrentPositionEx(ULONG h, PVOID p)
{
    return state_pair(h, p, PAIR_POSITION);
}

BOOL GetViewportOrgEx(ULONG h, PVOID p)
{
    return state_pair(h, p, PAIR_VIEWPORT_ORIGIN);
}

BOOL GetWindowOrgEx(ULONG h, PVOID p)
{
    return state_pair(h, p, PAIR_WINDOW_ORIGIN);
}

BOOL GetViewportExtEx(ULONG h, PVOID p)
{
    return state_pair(h, p, PAIR_VIEWPORT_EXTENT);
}

BOOL GetWindowExtEx(ULONG h, PVOID p)
{
    return state_pair(h, p, PAIR_WINDOW_EXTENT);
}

ULONG GetBkColor(ULONG h)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
        open32SetError(ERROR_INVALID_HANDLE);
    return dc ? dc->background_color & COLORREF_RGB_MASK : UINT32_MAX;
}

ULONG GetTextColor(ULONG h)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
        open32SetError(ERROR_INVALID_HANDLE);
    return dc ? dc->text_color : UINT32_MAX;
}

LONG GetBkMode(ULONG h)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
        open32SetError(ERROR_INVALID_HANDLE);
    return dc ? dc->background_mode : 0;
}

LONG GetPolyFillMode(ULONG h)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
        open32SetError(ERROR_INVALID_HANDLE);
    return dc ? dc->poly_fill_mode : 0;
}

LONG SetPolyFillMode(ULONG h, LONG value)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || value < ALTERNATE || value > WINDING)
    {
        open32SetError(dc ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
        return 0;
    }
    LONG old = dc->poly_fill_mode;
    dc->poly_fill_mode = value;
    open32MetafileWords(h, WIN32_META_SETPOLYFILLMODE, 1, &value);
    return old;
}

LONG SetStretchBltMode(ULONG h, LONG value)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    /* PMWINX maps BLACKONWHITE, WHITEONBLACK and COLORONCOLOR; any other mode returns 0. */
    if (value < BLACKONWHITE || value > COLORONCOLOR)
        return 0;
    LONG old = dc->stretch_mode;
    dc->stretch_mode = value;
    open32MetafileWords(h, WIN32_META_SETSTRETCHBLTMODE, 1, &value);
    return old;
}

LONG SetTextCharacterExtra(ULONG h, LONG value)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return INT32_MIN;
    }
    LONG old = dc->character_extra;
    dc->character_extra = value;
    open32MetafileWords(h, WIN32_META_SETTEXTCHAREXTRA, 1, &value);
    return old;
}

BOOL SetTextJustification(ULONG h, LONG extra, LONG breaks)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || breaks < 0)
    {
        open32SetError(dc ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
        return FALSE;
    }
    dc->justify_extra = breaks ? extra : 0;
    dc->justify_breaks = breaks;
    const LONG words[] = {extra, breaks};
    open32MetafileWords(h, WIN32_META_SETTEXTJUSTIFICATION, 2, words);
    return TRUE;
}

ULONG GetCurrentObject(ULONG h, ULONG type)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    switch (type)
    {
        case WIN32_OBJ_PEN:
            return dc->pen_handle;
        case WIN32_OBJ_BRUSH:
            return dc->brush_handle;
        case WIN32_OBJ_PAL:
            return dc->palette_handle;
        case WIN32_OBJ_FONT:
            return dc->font_handle;
        case WIN32_OBJ_BITMAP:
        {
            PresentationSpace *ps = lx_pm_font_space(h);
            PmBitmap *b = ps ? ps->bitmap_lease.resource : NULL;
            return b ? b->handle : 0;
        }
        default:
            open32SetError(ERROR_INVALID_HANDLE);
            return 0;
    }
}

ULONG WindowFromDC(ULONG h)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
        open32SetError(ERROR_INVALID_HANDLE);
    return dc && dc->window ? open32PublicWindow(dc->window) : 0;
}

static BOOL offset_origin(ULONG h, LONG x, LONG y, PVOID old, BOOL viewport)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    int64_t a = (int64_t)(viewport ? dc->viewport_x : dc->window_x) + x;
    int64_t b = (int64_t)(viewport ? dc->viewport_y : dc->window_y) + y;
    if (a < INT32_MIN || a > INT32_MAX || b < INT32_MIN || b > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return viewport ? SetViewportOrgEx(h, (LONG)a, (LONG)b, old)
                    : SetWindowOrgEx(h, (LONG)a, (LONG)b, old);
}

BOOL OffsetViewportOrgEx(ULONG h, LONG x, LONG y, PVOID old)
{
    return offset_origin(h, x, y, old, TRUE);
}

BOOL OffsetWindowOrgEx(ULONG h, LONG x, LONG y, PVOID old)
{
    return offset_origin(h, x, y, old, FALSE);
}

static BOOL scale_extent(ULONG h, LONG xn, LONG xd, LONG yn, LONG yd, PVOID old, BOOL viewport)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || (!viewport && (!xn || !yn)))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    /* Viewport scaling outside the free modes, or with a zero factor, changes nothing. */
    if (viewport && (!xn || !xd || !yn || !yd || dc->map_mode < WIN32_MM_ISOTROPIC))
        return TRUE;
    if (!xd || !yd)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    double scaled_x = (double)(viewport ? dc->viewport_width : dc->window_width) * xn / xd;
    double scaled_y = (double)(viewport ? dc->viewport_height : dc->window_height) * yn / yd;
    if (fabs(scaled_x) > INT32_MAX || fabs(scaled_y) > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LONG x = (LONG)lround(scaled_x), y = (LONG)lround(scaled_y);
    return viewport ? SetViewportExtEx(h, x, y, old) : SetWindowExtEx(h, x, y, old);
}

BOOL ScaleViewportExtEx(ULONG h, LONG xn, LONG xd, LONG yn, LONG yd, PVOID old)
{
    return scale_extent(h, xn, xd, yn, yd, old, TRUE);
}

BOOL ScaleWindowExtEx(ULONG h, LONG xn, LONG xd, LONG yn, LONG yd, PVOID old)
{
    return scale_extent(h, xn, xd, yn, yd, old, FALSE);
}

ULONG SetPixel(ULONG h, LONG x, LONG y, ULONG color)
{
    Open32DC *dc = open32FindDC(h);
    if (dc)
        open32MetafilePixel(h, x, y, color);
    if (!dc || !open32MapPoint(dc, &x, &y, TRUE))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return UINT32_MAX;
    }
    ULONG nearest = GetNearestColor(h, color);
    if (nearest == UINT32_MAX)
        return nearest;
    uint32 rgb = (nearest & 255) << 16 | (nearest & 0xff00) | (nearest >> 16 & 255);
    if (x < 0 || y < 0 || x >= open32DCWidth(h) || y >= open32DCHeight(h) ||
        !lx_pm_raster_write(h, x, open32DCHeight(h) - 1 - y, 1, 1, &rgb))
        return UINT32_MAX;
    return nearest;
}

LONG SetROP2(ULONG handle, LONG mode)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || mode < R2_BLACK || mode > R2_WHITE)
    {
        open32SetError(dc ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
        return 0;
    }
    LONG previous = dc->rop2;
    dc->rop2 = mode;
    open32MetafileWords(handle, WIN32_META_SETROP2, 1, &mode);
    return previous;
}

LONG GetROP2(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    return dc->rop2;
}
