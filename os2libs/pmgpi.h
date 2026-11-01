/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_PMGPI_H_
#define _INCL_PMGPI_H_

#include "os2types.h"
#include "pmwin.h"
#include "pmgpi_fonts.h"

#ifdef __cplusplus
extern "C"
{
#endif

    enum
    {
        PU_PELS = 8,
        GPIF_DEFAULT = 0,
        GPIT_NORMAL = 0,
        GPIA_ASSOC = 0x4000,
        DM_DRAW = 1
    };
    OS2EXPORT BOOL OS2API DevQueryCaps(HDC dc, LONG start, LONG count, PLONG output)
        OS2APIINFO(606);
    OS2EXPORT LONG OS2API GpiQueryFonts(HPS hps, ULONG options, PSZ face, PLONG requested,
                                        LONG record_size, PFONTMETRICS output) OS2APIINFO(586);
    OS2EXPORT BOOL OS2API GpiQueryFontMetrics(HPS hps, LONG record_size, PFONTMETRICS output)
        OS2APIINFO(453);
    OS2EXPORT LONG OS2API GpiQueryColorIndex(HPS hps, ULONG options, LONG rgb) OS2APIINFO(439);
    OS2EXPORT LONG OS2API GpiQueryRGBColor(HPS hps, ULONG options, LONG index) OS2APIINFO(479);
    OS2EXPORT BOOL OS2API GpiErase(HPS hps) OS2APIINFO(389);
    OS2EXPORT HPS OS2API GpiCreatePS(HAB hab, HDC dc, PSIZEL size, ULONG flags) OS2APIINFO(369);
    OS2EXPORT BOOL OS2API GpiDestroyPS(HPS hps) OS2APIINFO(379);
    OS2EXPORT LONG OS2API GpiLine(HPS hps, PPOINTL point) OS2APIINFO(398);
    OS2EXPORT BOOL OS2API GpiMove(HPS hps, PPOINTL point) OS2APIINFO(404);
    OS2EXPORT BOOL OS2API GpiSetColor(HPS hps, LONG color) OS2APIINFO(517);
    OS2EXPORT BOOL OS2API GpiSetDrawingMode(HPS hps, LONG mode) OS2APIINFO(522);
    OS2EXPORT LONG OS2API GpiSetPel(HPS hps, PPOINTL point) OS2APIINFO(544);
    OS2EXPORT BOOL OS2API GpiQueryTextBox(HPS hps, LONG lCount1, PCH pchString, LONG lCount2,
                                          PPOINTL aptlPoints) OS2APIINFO(489);

#ifdef __cplusplus
}
#endif

#endif
