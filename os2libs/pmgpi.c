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

BOOL GpiQueryTextBox(HPS hps, LONG lCount1, PCH pchString, LONG lCount2, PPOINTL aptlPoints)
{
    TRACE_NATIVE("GpiQueryTextBox(%u, %d, '%s', %d, %p)", (unsigned int)hps, (int)lCount1,
                 pchString, (int)lCount2, aptlPoints);
    FIXME("write me");
    return 0;
}

HPS GpiCreatePS(HAB hab, HDC dc, PSIZEL size, ULONG flags)
{
    if (!size || (flags & ~(PU_PELS | GPIA_ASSOC)))
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
