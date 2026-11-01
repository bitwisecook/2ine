/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native.h"
#include "pmgpi.h"
#include "os2libs/pmwin/graphics.h"

#include "os2libs/generated/pmgpi-lx.h"

HPS GpiCreatePS(HAB hab, HDC dc, PSIZEL size, ULONG flags)
{
    TRACE_NATIVE("GpiCreatePS(%u, %u, %p, %u)", (uint)hab, (uint)dc, size, (uint)flags);
    if (!size || (flags & ~(PU_PELS | GPIA_ASSOC | 0x1000 | 0x300)) || (flags & 0x300) == 0x300 ||
        size->cx < 0 || size->cy < 0)
        return 0;
    return lx_pm_create_ps(hab, dc);
}
BOOL GpiDestroyPS(HPS hps)
{
    return lx_pm_destroy_ps(hps);
}
LONG GpiLine(HPS hps, PPOINTL point)
{
    return lx_pm_line(hps, point, 0);
}
BOOL GpiMove(HPS hps, PPOINTL point)
{
    return lx_pm_move(hps, point);
}
BOOL GpiSetColor(HPS hps, LONG color)
{
    return lx_pm_color(hps, color) != -255;
}
HDC GpiQueryDevice(HPS ps)
{
    return lx_pm_query_dc(ps);
}
BOOL GpiAssociate(HPS ps, HDC dc)
{
    return lx_pm_associate_dc(ps, dc);
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
    return lx_pm_erase(hps);
}

LONG GpiQueryRGBColor(HPS hps, ULONG options, LONG index)
{
    return !options && lx_pm_valid_ps(hps) ? lx_pm_rgb(index) : -255;
}
LONG GpiQueryColorIndex(HPS hps, ULONG options, LONG rgb)
{
    if (options || !lx_pm_valid_ps(hps) || rgb < 0 || rgb > 0xffffff)
        return -255;
    LONG nearest = CLR_WHITE;
    ULONG best = UINT32_MAX;
    for (LONG index = CLR_WHITE; index <= CLR_PALEGRAY; index++)
    {
        LONG candidate = lx_pm_rgb(index);
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
