/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* PMGPI's export table and the presentation-space, line, colour and code-page entries. */

#include "os2/include/private/export.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmdev.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/private/pmwinp.h"
#include "os2/ps/pmgpi/internal.h"

#include "os2/ps/pmgpi/generated/pmgpi-lx.h"

/* Bounds the host work for one guest call. */
enum
{
    MAX_POLYLINE_POINTS = 65536
};

HPS GpiCreatePS(HAB hab, HDC dc, PSIZEL size, ULONG flags)
{
    TRACE_NATIVE("GpiCreatePS(%u, %u, %p, %u)", (uint)hab, (uint)dc, size, (uint)flags);
    if (!size || (flags & ~(GPI_PAGE_UNITS | GPIA_ASSOC | GPIT_MICRO | GPIF_SHORT | GPIF_LONG)) ||
        (flags & (GPIF_SHORT | GPIF_LONG)) == (GPIF_SHORT | GPIF_LONG) || size->cx < 0 ||
        size->cy < 0)
        return 0;
    HPS handle = lx_pm_create_ps(hab, dc);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (ps)
    {
        ps->page_size = *size;
        ps->page_units = flags & GPI_PAGE_UNITS;
        ps->ps_options = flags;
    }
    return handle;
}

BOOL GpiDestroyPS(HPS hps)
{
    return lx_pm_destroy_ps(hps);
}

LONG GpiLine(HPS hps, PPOINTL point)
{
    TRACE_NATIVE("GpiLine(%u, %d,%d)", (uint)hps, point ? (int)point->x : 0,
                 point ? (int)point->y : 0);
    return lx_pm_line(hps, point, 0);
}

LONG GpiPolyLine(HPS hps, LONG count, PPOINTL points)
{
    TRACE_NATIVE("GpiPolyLine(%u, %d, %d,%d)", (uint)hps, (int)count, points ? (int)points[0].x : 0,
                 points ? (int)points[0].y : 0);
    TRACE_NATIVE("GpiPolyLine(%u, %d, %p)", (unsigned)hps, (int)count, points);
    if (!lx_pm_valid_ps(hps) || count < 0 || count > MAX_POLYLINE_POINTS || (count && !points))
        return GPI_ERROR;
    LONG result = GPI_OK;
    for (LONG i = 0; i < count; i++)
    {
        LONG drawn = lx_pm_line(hps, &points[i], 0);
        if (!drawn)
            return GPI_ERROR;
        if (drawn == GPI_HITS)
            result = GPI_HITS;
    }
    return result;
}

BOOL GpiMove(HPS hps, PPOINTL point)
{
    TRACE_NATIVE("GpiMove(%u, %d,%d)", (uint)hps, point ? (int)point->x : 0,
                 point ? (int)point->y : 0);
    return lx_pm_move(hps, point);
}

BOOL GpiSetColor(HPS hps, LONG color)
{
    return lx_pm_color(hps, color) != CLR_ERROR;
}

HDC GpiQueryDevice(HPS ps)
{
    return lx_pm_query_dc(ps);
}

BOOL GpiAssociate(HPS ps, HDC dc)
{
    if (!lx_pm_associate_dc(ps, dc))
        return FALSE;
    /* Association returns the viewing limits to their default value. */
    PresentationSpace *space = lx_pm_font_space(ps);
    if (space)
    {
        space->gpi.viewing_limits = space->gpi.default_limits;
        space->gpi.viewing_set = space->gpi.default_set;
        space->gpi.viewing_custom = FALSE;
    }
    return TRUE;
}

BOOL GpiSetCp(HPS ps, ULONG cp)
{
    return lx_pm_set_cp(ps, cp);
}

ULONG GpiQueryCp(HPS ps)
{
    return lx_pm_query_cp(ps);
}

BOOL GpiCreateLogColorTable(HPS ps, ULONG options, LONG format, LONG start, LONG count,
                            PLONG colors)
{
    return lx_pm_color_table(ps, options, format, start, count, colors);
}

BOOL GpiSetDrawingMode(HPS hps, LONG mode)
{
    return mode == DM_DRAW && lx_pm_valid_ps(hps);
}

LONG GpiSetPel(HPS hps, PPOINTL point)
{
    return lx_pm_line(hps, point, 1);
}

BOOL GpiErase(HPS hps)
{
    TRACE_NATIVE("GpiErase(%u)", (uint)hps);
    return lx_pm_erase(hps);
}

LONG GpiQueryRGBColor(HPS hps, ULONG options, LONG index)
{
    /* The true-color host realizes every logical RGB value exactly. INDEX and
       REALIZED therefore agree, but both still use this PS's logical table. */
    return !(options & ~(LCOLOPT_REALIZED | LCOLOPT_INDEX)) && index != CLR_DEFAULT &&
                   lx_pm_valid_ps(hps)
               ? lx_pm_resolve_color(hps, index)
               : RGB_ERROR;
}

LONG GpiQueryColorIndex(HPS hps, ULONG options, LONG rgb)
{
    PresentationSpace *ps = lx_pm_font_space(hps);
    if ((options & ~LCOLOPT_REALIZED) || !ps || rgb < 0 || rgb > RGB_WHITE)
        return CLR_ERROR;
    if (ps->rgb_colors)
        return rgb;
    LONG nearest = CLR_WHITE;
    ULONG best = UINT32_MAX;
    for (LONG index = CLR_WHITE; index < (LONG)(sizeof ps->colors / sizeof *ps->colors); index++)
    {
        LONG candidate = lx_pm_resolve_color(hps, index);
        if (candidate < 0)
            continue;
        LONG r = ((rgb >> 16) & 255) - ((candidate >> 16) & 255);
        LONG g = ((rgb >> 8) & 255) - ((candidate >> 8) & 255);
        LONG b = (rgb & 255) - (candidate & 255);
        ULONG distance = (ULONG)(r * r + g * g + b * b);
        if (distance < best)
        {
            best = distance;
            nearest = index;
        }
    }
    return nearest;
}
