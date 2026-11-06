/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 DC state follows PMWINX's validated-handle, previous-value convention. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmwin/internal.h"
#include <math.h>
static BOOL state_pair(ULONG handle, PVOID output, unsigned kind)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !output)
    {
        open32SetError(dc ? 87 : 6);
        return FALSE;
    }
    LONG x, y;
    switch (kind)
    {
        case 0:
            x = dc->position_x;
            y = dc->position_y;
            break;
        case 1:
            x = dc->viewport_x;
            y = dc->viewport_y;
            break;
        case 2:
            x = dc->window_x;
            y = dc->window_y;
            break;
        case 3:
            x = dc->viewport_width;
            y = dc->viewport_height;
            break;
        default:
            x = dc->window_width;
            y = dc->window_height;
            break;
    }
    lx_guest_store_u32(output, (ULONG)x);
    lx_guest_store_u32((uint8 *)output + 4, (ULONG)y);
    return TRUE;
}
BOOL GetCurrentPositionEx(ULONG h, PVOID p)
{
    return state_pair(h, p, 0);
}
BOOL GetViewportOrgEx(ULONG h, PVOID p)
{
    return state_pair(h, p, 1);
}
BOOL GetWindowOrgEx(ULONG h, PVOID p)
{
    return state_pair(h, p, 2);
}
BOOL GetViewportExtEx(ULONG h, PVOID p)
{
    return state_pair(h, p, 3);
}
BOOL GetWindowExtEx(ULONG h, PVOID p)
{
    return state_pair(h, p, 4);
}
ULONG GetBkColor(ULONG h)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
        open32SetError(6);
    return dc ? dc->background_color & 0xFFFFFF : UINT32_MAX;
}
ULONG GetTextColor(ULONG h)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
        open32SetError(6);
    return dc ? dc->text_color : UINT32_MAX;
}
LONG GetBkMode(ULONG h)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
        open32SetError(6);
    return dc ? dc->background_mode : 0;
}
LONG GetPolyFillMode(ULONG h)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
        open32SetError(6);
    return dc ? dc->poly_fill_mode : 0;
}
LONG SetPolyFillMode(ULONG h, LONG value)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || value < 1 || value > 2)
    {
        open32SetError(dc ? 87 : 6);
        return 0;
    }
    LONG old = dc->poly_fill_mode;
    dc->poly_fill_mode = value;
    open32MetafileWords(h, 0x106, 1, &value);
    return old;
}
LONG SetStretchBltMode(ULONG h, LONG value)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
    {
        open32SetError(6);
        return 0;
    }
    /* PMWINX maps BLACKONWHITE, WHITEONBLACK and COLORONCOLOR; any other mode returns 0. */
    if (value < 1 || value > 3)
        return 0;
    LONG old = dc->stretch_mode;
    dc->stretch_mode = value;
    open32MetafileWords(h, 0x107, 1, &value);
    return old;
}
LONG SetTextCharacterExtra(ULONG h, LONG value)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
    {
        open32SetError(6);
        return INT32_MIN;
    }
    LONG old = dc->character_extra;
    dc->character_extra = value;
    open32MetafileWords(h, 0x108, 1, &value);
    return old;
}
BOOL SetTextJustification(ULONG h, LONG extra, LONG breaks)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || breaks < 0)
    {
        open32SetError(dc ? 87 : 6);
        return FALSE;
    }
    dc->justify_extra = breaks ? extra : 0;
    dc->justify_breaks = breaks;
    const LONG words[] = {extra, breaks};
    open32MetafileWords(h, 0x20a, 2, words);
    return TRUE;
}
ULONG GetCurrentObject(ULONG h, ULONG type)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
    {
        open32SetError(6);
        return 0;
    }
    switch (type)
    {
        case 1:
            return dc->pen_handle;
        case 2:
            return dc->brush_handle;
        case 5:
            return dc->palette_handle;
        case 6:
            return dc->font_handle;
        case 7:
        {
            PresentationSpace *ps = lx_pm_font_space(h);
            PmBitmap *b = ps ? ps->bitmap_lease.resource : NULL;
            return b ? b->handle : 0;
        }
        default:
            open32SetError(6);
            return 0;
    }
}
ULONG WindowFromDC(ULONG h)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
        open32SetError(6);
    return dc && dc->window ? open32PublicWindow(dc->window) : 0;
}
static BOOL offset_origin(ULONG h, LONG x, LONG y, PVOID old, BOOL viewport)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
    {
        open32SetError(6);
        return FALSE;
    }
    int64_t a = (int64_t)(viewport ? dc->viewport_x : dc->window_x) + x;
    int64_t b = (int64_t)(viewport ? dc->viewport_y : dc->window_y) + y;
    if (a < INT32_MIN || a > INT32_MAX || b < INT32_MIN || b > INT32_MAX)
    {
        open32SetError(87);
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
        open32SetError(87);
        return FALSE;
    }
    /* Viewport scaling outside the free modes, or with a zero factor, changes nothing. */
    if (viewport && (!xn || !xd || !yn || !yd || dc->map_mode < 7))
        return TRUE;
    if (!xd || !yd)
    {
        open32SetError(87);
        return FALSE;
    }
    double scaled_x = (double)(viewport ? dc->viewport_width : dc->window_width) * xn / xd;
    double scaled_y = (double)(viewport ? dc->viewport_height : dc->window_height) * yn / yd;
    if (fabs(scaled_x) > INT32_MAX || fabs(scaled_y) > INT32_MAX)
    {
        open32SetError(87);
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
        open32SetError(6);
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
