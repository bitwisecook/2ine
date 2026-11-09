/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_PMGPI_H_
#define _INCL_PMGPI_H_

#include "os2/include/os2def.h"
#include "os2/include/pmwin.h"
#include "os2/include/pmfont.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Common GPI definitions */

    typedef struct
    {
        LONG cx, cy;
    } SIZEL, *PSIZEL;

    /* INCL_GPICONTROL */

    enum
    {
        PU_PELS = 8,
        GPIF_DEFAULT = 0,
        GPIT_NORMAL = 0,
        GPIA_ASSOC = 0x4000,
        DM_DRAW = 1
    };

    OS2EXPORT LONG OS2API GpiSavePS(HPS ps);

    OS2EXPORT BOOL OS2API GpiRestorePS(HPS ps, LONG id);

    OS2EXPORT BOOL OS2API GpiSetDrawControl(HPS ps, LONG control, LONG value);

    OS2EXPORT HDC OS2API GpiQueryDevice(HPS ps);

    OS2EXPORT BOOL OS2API GpiAssociate(HPS ps, HDC dc);

    OS2EXPORT BOOL OS2API GpiErase(HPS hps);

    OS2EXPORT HPS OS2API GpiCreatePS(HAB hab, HDC dc, PSIZEL size, ULONG flags);

    OS2EXPORT BOOL OS2API GpiDestroyPS(HPS hps);

    OS2EXPORT BOOL OS2API GpiSetDrawingMode(HPS hps, LONG mode);

    OS2EXPORT ULONG OS2API GpiQueryPS(HPS a0, PSIZEL a1);

    OS2EXPORT BOOL OS2API GpiResetPS(HPS a0, ULONG a1);

    OS2EXPORT BOOL OS2API GpiSetPS(HPS a0, PSIZEL a1, ULONG a2);

    OS2EXPORT BOOL OS2API GpiSetStopDraw(HPS a0, LONG a1);

    OS2EXPORT LONG OS2API GpiQueryStopDraw(HPS a0);

    /* INCL_GPICORRELATION */

    OS2EXPORT BOOL OS2API GpiQueryBoundaryData(HPS ps, PRECTL bounds);

    OS2EXPORT BOOL OS2API GpiQueryPickAperturePosition(HPS a0, PPOINTL a1);

    OS2EXPORT BOOL OS2API GpiQueryPickApertureSize(HPS a0, PSIZEL a1);

    OS2EXPORT BOOL OS2API GpiResetBoundaryData(HPS a0);

    OS2EXPORT BOOL OS2API GpiSetPickAperturePosition(HPS a0, PPOINTL a1);

    OS2EXPORT BOOL OS2API GpiSetPickApertureSize(HPS a0, LONG a1, PSIZEL a2);

    /* INCL_GPITRANSFORMS */

    OS2EXPORT BOOL OS2API GpiConvert(HPS ps, LONG source, LONG target, LONG count, PPOINTL points);

    OS2EXPORT BOOL OS2API GpiSetPageViewport(HPS ps, PRECTL rect);

    OS2EXPORT BOOL OS2API GpiQueryPageViewport(HPS ps, PRECTL rect);

    OS2EXPORT BOOL OS2API GpiSetDefaultViewMatrix(HPS ps, LONG count, PVOID matrix, LONG options);

    OS2EXPORT BOOL OS2API GpiQueryDefaultViewMatrix(HPS a0, LONG a1, PVOID a2);

    OS2EXPORT BOOL OS2API GpiQueryGraphicsField(HPS a0, PRECTL a1);

    OS2EXPORT BOOL OS2API GpiQueryModelTransformMatrix(HPS a0, LONG a1, PVOID a2);

    OS2EXPORT BOOL OS2API GpiQueryViewingLimits(HPS a0, PRECTL a1);

    OS2EXPORT BOOL OS2API GpiSetGraphicsField(HPS a0, PRECTL a1);

    OS2EXPORT BOOL OS2API GpiSetModelTransformMatrix(HPS a0, LONG a1, PVOID a2, LONG a3);

    OS2EXPORT BOOL OS2API GpiSetViewingLimits(HPS a0, PRECTL a1);

    OS2EXPORT BOOL OS2API GpiTranslate(HPS a0, PVOID a1, LONG a2, PPOINTL a3);

    OS2EXPORT BOOL OS2API GpiScale(HPS a0, PVOID a1, LONG a2, PLONG a3, PPOINTL a4);

    OS2EXPORT BOOL OS2API GpiRotate(HPS a0, PVOID a1, LONG a2, LONG a3, PPOINTL a4);

    /* INCL_GPIPATHS */

    enum
    {
        FPATH_ALTERNATE = 0,
        FPATH_WINDING = 2,
        FPATH_INCL = 0,
        FPATH_EXCL = 8
    };

    OS2EXPORT BOOL OS2API GpiBeginPath(HPS a0, LONG a1);

    OS2EXPORT BOOL OS2API GpiCloseFigure(HPS a0);

    OS2EXPORT BOOL OS2API GpiEndPath(HPS a0);

    OS2EXPORT LONG OS2API GpiFillPath(HPS a0, LONG a1, LONG a2);

    OS2EXPORT LONG OS2API GpiStrokePath(HPS a0, LONG a1, ULONG a2);

    OS2EXPORT ULONG OS2API GpiPathToRegion(HPS a0, LONG a1, LONG a2);

    /* INCL_GPILOGCOLORTABLE */

    OS2EXPORT BOOL OS2API GpiQueryColorData(HPS ps, LONG count, PLONG output);

    OS2EXPORT BOOL OS2API GpiCreateLogColorTable(HPS ps, ULONG options, LONG format, LONG start,
                                                 LONG count, PLONG colors);

    OS2EXPORT LONG OS2API GpiQueryColorIndex(HPS hps, ULONG options, LONG rgb);

    OS2EXPORT LONG OS2API GpiQueryRGBColor(HPS hps, ULONG options, LONG index);

    OS2EXPORT LONG OS2API GpiQueryLogColorTable(HPS ps, ULONG options, LONG start, LONG count,
                                                PLONG output);

    OS2EXPORT LONG OS2API GpiQueryNearestColor(HPS a0, ULONG a1, LONG a2);

    OS2EXPORT LONG OS2API GpiQueryRealColors(HPS a0, ULONG a1, LONG a2, LONG a3, PLONG a4);

    OS2EXPORT BOOL OS2API GpiDeletePalette(ULONG a0);

    OS2EXPORT ULONG OS2API GpiSelectPalette(HPS a0, ULONG a1);

    OS2EXPORT ULONG OS2API GpiQueryPalette(HPS a0);

    OS2EXPORT ULONG OS2API GpiCreatePalette(HAB a0, ULONG a1, ULONG a2, ULONG a3, PULONG a4);

    OS2EXPORT LONG OS2API GpiQueryPaletteInfo(ULONG a0, HPS a1, ULONG a2, ULONG a3, ULONG a4,
                                              PULONG a5);

    /* INCL_GPIBITMAPS */

    enum
    {
        ROP_SRCCOPY = 0x00cc,
        ROP_SRCPAINT = 0x00ee,
        ROP_SRCAND = 0x0088,
        ROP_SRCINVERT = 0x0066,
        ROP_SRCERASE = 0x0044,
        ROP_NOTSRCCOPY = 0x0033,
        ROP_NOTSRCERASE = 0x0011,
        ROP_MERGECOPY = 0x00c0,
        ROP_MERGEPAINT = 0x00bb,
        ROP_PATCOPY = 0x00f0,
        ROP_PATPAINT = 0x00fb,
        ROP_PATINVERT = 0x005a,
        ROP_DSTINVERT = 0x0055,
        ROP_ZERO = 0x0000,
        ROP_ONE = 0x00ff,
        BBO_OR = 0,
        BBO_AND = 1,
        BBO_IGNORE = 2
    };

    OS2EXPORT LONG OS2API GpiSetBitmapBits(HPS ps, LONG start, LONG scans, PVOID bits, PVOID info);

    OS2EXPORT LONG OS2API GpiQueryBitmapBits(HPS ps, LONG start, LONG scans, PVOID bits,
                                             PVOID info);

    OS2EXPORT LONG OS2API GpiDrawBits(HPS ps, PVOID bits, PVOID info, LONG count, PPOINTL points,
                                      LONG rop, ULONG options);

    OS2EXPORT ULONG OS2API GpiCreateBitmap(HPS ps, PVOID header, ULONG options, PVOID bits,
                                           PVOID info);

    OS2EXPORT ULONG OS2API GpiLoadBitmap(HPS ps, HMODULE module, ULONG id, LONG width, LONG height);

    OS2EXPORT BOOL OS2API GpiDeleteBitmap(ULONG bitmap);

    OS2EXPORT ULONG OS2API GpiSetBitmap(HPS ps, ULONG bitmap);

    OS2EXPORT BOOL OS2API GpiQueryBitmapParameters(ULONG bitmap, PVOID data);

    OS2EXPORT BOOL OS2API GpiQueryBitmapInfoHeader(ULONG bitmap, PVOID data);

    OS2EXPORT BOOL OS2API GpiQueryDeviceBitmapFormats(HPS ps, LONG count, PLONG output);

    OS2EXPORT LONG OS2API GpiBitBlt(HPS destination, HPS source, LONG count, PPOINTL points,
                                    LONG rop, ULONG options);

    OS2EXPORT LONG OS2API GpiWCBitBlt(HPS destination, ULONG bitmap, LONG count, PPOINTL points,
                                      LONG rop, ULONG options);

    OS2EXPORT BOOL OS2API GpiSetBitmapId(HPS ps, ULONG bitmap, LONG id);

    OS2EXPORT ULONG OS2API GpiQueryBitmapHandle(HPS ps, LONG id);

    OS2EXPORT LONG OS2API GpiSetPel(HPS hps, PPOINTL point);

    OS2EXPORT LONG OS2API GpiQueryPel(HPS a0, PPOINTL a1);

    /* INCL_GPIPRIMITIVES */

    enum
    {
        CLR_FALSE = -5,
        CLR_TRUE = -4,
        CLR_DEFAULT = -3,
        CLR_WHITE = -2,
        CLR_BLACK = -1,
        CLR_BACKGROUND = 0,
        CLR_BLUE = 1,
        CLR_RED = 2,
        CLR_PINK = 3,
        CLR_GREEN = 4,
        CLR_CYAN = 5,
        CLR_YELLOW = 6,
        CLR_NEUTRAL = 7,
        CLR_DARKGRAY = 8,
        CLR_DARKBLUE = 9,
        CLR_DARKRED = 10,
        CLR_DARKPINK = 11,
        CLR_DARKGREEN = 12,
        CLR_DARKCYAN = 13,
        CLR_BROWN = 14,
        CLR_PALEGRAY = 15
    };

    enum
    {
        FM_DEFAULT = 0,
        FM_OR = 1,
        FM_OVERPAINT = 2,
        FM_XOR = 4,
        FM_LEAVEALONE = 5,
        FM_AND = 6,
        FM_SUBTRACT = 7,
        FM_MASKSRCNOT = 8,
        FM_ZERO = 9,
        FM_NOTMERGESRC = 10,
        FM_NOTXORSRC = 11,
        FM_INVERT = 12,
        FM_MERGESRCNOT = 13,
        FM_NOTCOPYSRC = 14,
        FM_MERGENOTSRC = 15,
        FM_NOTMASKSRC = 16,
        FM_ONE = 17,
        BM_DEFAULT = 0,
        BM_OVERPAINT = 2,
        BM_LEAVEALONE = 5,
        BM_SRCTRANSPARENT = 18,
        BM_DESTTRANSPARENT = 19,
        LINETYPE_SOLID = 7,
        LINETYPE_INVISIBLE = 8,
        PATSYM_DEFAULT = 0,
        PATSYM_NOSHADE = 15,
        PATSYM_SOLID = 16,
        PATSYM_BLANK = 64,
        BA_NOBOUNDARY = 0,
        BA_BOUNDARY = 1,
        BA_ALTERNATE = 0,
        BA_WINDING = 2,
        BA_INCL = 0,
        BA_EXCL = 8
    };

    typedef struct
    {
        LONG cx, cy;
    } SIZEF;

    typedef SIZEF *PSIZEF;

    typedef struct LXPOLYGON
    {
        ULONG count;
        PPOINTL points;
    } LXPOLYGON;

    OS2EXPORT BOOL OS2API GpiSetCharSet(HPS ps, LONG id);

    OS2EXPORT LONG OS2API GpiQueryCharSet(HPS ps);

    OS2EXPORT BOOL OS2API GpiSetCharBox(HPS ps, PSIZEF box);

    OS2EXPORT BOOL OS2API GpiSetCharAngle(HPS ps, PPOINTL angle);

    OS2EXPORT BOOL OS2API GpiSetCharShear(HPS ps, PPOINTL shear);

    OS2EXPORT BOOL OS2API GpiSetCharDirection(HPS ps, LONG direction);

    OS2EXPORT LONG OS2API GpiQueryCharDirection(HPS ps);

    OS2EXPORT BOOL OS2API GpiSetTextAlignment(HPS ps, LONG horizontal, LONG vertical);

    OS2EXPORT BOOL OS2API GpiQueryTextAlignment(HPS ps, PLONG horizontal, PLONG vertical);

    OS2EXPORT BOOL OS2API GpiQueryCharShear(HPS ps, PPOINTL shear);

    OS2EXPORT BOOL OS2API GpiQueryCharAngle(HPS ps, PPOINTL angle);

    OS2EXPORT BOOL OS2API GpiQueryCharBox(HPS ps, PSIZEF box);

    OS2EXPORT BOOL OS2API GpiSetBackMix(HPS ps, LONG mode);

    OS2EXPORT LONG OS2API GpiQueryBackMix(HPS ps);

    OS2EXPORT BOOL OS2API GpiSetMix(HPS ps, LONG mode);

    OS2EXPORT LONG OS2API GpiQueryMix(HPS ps);

    OS2EXPORT BOOL OS2API GpiSetBackColor(HPS ps, LONG color);

    OS2EXPORT LONG OS2API GpiQueryBackColor(HPS ps);

    OS2EXPORT BOOL OS2API GpiSetLineType(HPS ps, LONG type);

    OS2EXPORT LONG OS2API GpiQueryLineType(HPS ps);

    OS2EXPORT BOOL OS2API GpiSetAttrs(HPS ps, LONG primitive, ULONG mask, ULONG defaults,
                                      PVOID attrs);

    OS2EXPORT LONG OS2API GpiCharString(HPS ps, LONG count, PCH text);

    OS2EXPORT LONG OS2API GpiCharStringAt(HPS ps, PPOINTL point, LONG count, PCH text);

    OS2EXPORT LONG OS2API GpiQueryPatternSet(HPS ps);

    OS2EXPORT BOOL OS2API GpiSetPatternRefPoint(HPS ps, PPOINTL point);

    OS2EXPORT BOOL OS2API GpiQueryPatternRefPoint(HPS ps, PPOINTL point);

    OS2EXPORT BOOL OS2API GpiSetPatternSet(HPS ps, LONG id);

    OS2EXPORT LONG OS2API GpiQueryPattern(HPS ps);

    OS2EXPORT BOOL OS2API GpiSetPattern(HPS ps, LONG symbol);

    OS2EXPORT BOOL OS2API GpiQueryCurrentPosition(HPS ps, PPOINTL point);

    OS2EXPORT BOOL OS2API GpiSetCurrentPosition(HPS ps, PPOINTL point);

    OS2EXPORT LONG OS2API GpiRectVisible(HPS ps, PRECTL rect);

    OS2EXPORT LONG OS2API GpiCharStringPosAt(HPS ps, PPOINTL start, PRECTL rect, ULONG flags,
                                             LONG count, PCH text, PLONG advances);

    OS2EXPORT LONG OS2API GpiCharStringPos(HPS ps, PRECTL rect, ULONG flags, LONG count, PCH text,
                                           PLONG advances);

    OS2EXPORT LONG OS2API GpiBox(HPS ps, LONG control, PPOINTL point, LONG horizontal,
                                 LONG vertical);

    OS2EXPORT LONG OS2API GpiPolyLine(HPS ps, LONG count, PPOINTL points);

    OS2EXPORT LONG OS2API GpiLine(HPS hps, PPOINTL point);

    OS2EXPORT BOOL OS2API GpiMove(HPS hps, PPOINTL point);

    OS2EXPORT BOOL OS2API GpiSetColor(HPS hps, LONG color);

    OS2EXPORT BOOL OS2API GpiQueryTextBox(HPS hps, LONG lCount1, PCH pchString, LONG lCount2,
                                          PPOINTL aptlPoints);

    OS2EXPORT LONG OS2API GpiQueryColor(HPS ps);

    OS2EXPORT BOOL OS2API GpiSetCharMode(HPS ps, LONG mode);

    OS2EXPORT LONG OS2API GpiQueryCharMode(HPS ps);

    OS2EXPORT BOOL OS2API GpiBeginArea(HPS a0, ULONG a1);

    OS2EXPORT LONG OS2API GpiEndArea(HPS a0);

    OS2EXPORT LONG OS2API GpiFullArc(HPS a0, LONG a1, LONG a2);

    OS2EXPORT LONG OS2API GpiPointArc(HPS a0, PPOINTL a1);

    OS2EXPORT LONG OS2API GpiQueryLineEnd(HPS a0);

    OS2EXPORT LONG OS2API GpiQueryLineJoin(HPS a0);

    OS2EXPORT LONG OS2API GpiQueryLineWidthGeom(HPS a0);

    OS2EXPORT BOOL OS2API GpiSetArcParams(HPS a0, PLONG a1);

    OS2EXPORT BOOL OS2API GpiSetLineEnd(HPS a0, LONG a1);

    OS2EXPORT BOOL OS2API GpiSetLineJoin(HPS a0, LONG a1);

    OS2EXPORT BOOL OS2API GpiSetLineWidthGeom(HPS a0, LONG a1);

    OS2EXPORT LONG OS2API GpiQueryAttrs(HPS a0, LONG a1, ULONG a2, PVOID a3);

    OS2EXPORT LONG OS2API GpiPartialArc(HPS a0, PPOINTL a1, LONG a2, LONG a3, LONG a4);

    OS2EXPORT BOOL OS2API GpiSetCharExtra(HPS a0, LONG a1);

    /* INCL_GPILCIDS */

    OS2EXPORT LONG OS2API GpiCreateLogFont(HPS ps, PVOID name, LONG id, PFATTRS attrs);

    OS2EXPORT BOOL OS2API GpiDeleteSetId(HPS ps, LONG id);

    OS2EXPORT LONG OS2API GpiQueryNumberSetIds(HPS ps);

    OS2EXPORT BOOL OS2API GpiQuerySetIds(HPS ps, LONG count, PLONG types, PVOID names, PLONG ids);

    OS2EXPORT BOOL OS2API GpiQueryWidthTable(HPS ps, LONG first, LONG count, PLONG output);

    OS2EXPORT BOOL OS2API GpiSetCp(HPS ps, ULONG codepage);

    OS2EXPORT ULONG OS2API GpiQueryCp(HPS ps);

    OS2EXPORT LONG OS2API GpiQueryFonts(HPS hps, ULONG options, PSZ face, PLONG requested,
                                        LONG record_size, PFONTMETRICS output);

    OS2EXPORT BOOL OS2API GpiQueryFontMetrics(HPS hps, LONG record_size, PFONTMETRICS output);

    OS2EXPORT BOOL OS2API GpiLoadPublicFonts(HAB a0, PSZ a1);

    OS2EXPORT BOOL OS2API GpiUnloadPublicFonts(HAB a0, PSZ a1);

    /* INCL_GPIREGIONS */

    typedef struct RGNRECT
    {
        ULONG ircStart, crc, crcReturned, ulDirection;
    } RGNRECT;

    OS2EXPORT BOOL OS2API GpiQueryRegionRects(HPS ps, ULONG region, PRECTL bounds, RGNRECT *control,
                                              PRECTL rects);

    OS2EXPORT ULONG OS2API GpiCreateRegion(HPS ps, LONG count, PRECTL rectangles);

    OS2EXPORT BOOL OS2API GpiDestroyRegion(HPS ps, ULONG region);

    OS2EXPORT LONG OS2API GpiCombineRegion(HPS ps, ULONG destination, ULONG first, ULONG second,
                                           LONG mode);

    OS2EXPORT BOOL OS2API GpiSetRegion(HPS ps, ULONG region, LONG count, PRECTL rectangles);

    OS2EXPORT LONG OS2API GpiQueryRegionBox(HPS ps, ULONG region, PRECTL rectangle);

    OS2EXPORT LONG OS2API GpiSetClipRegion(HPS ps, ULONG region, PULONG old);

    OS2EXPORT ULONG OS2API GpiQueryClipRegion(HPS ps);

    OS2EXPORT LONG OS2API GpiOffsetRegion(HPS ps, ULONG region, PPOINTL point);

    OS2EXPORT LONG OS2API GpiOffsetClipRegion(HPS ps, PPOINTL point);

    OS2EXPORT LONG OS2API GpiQueryClipBox(HPS ps, PRECTL rect);

    OS2EXPORT LONG OS2API GpiIntersectClipRectangle(HPS ps, PRECTL rect);

    OS2EXPORT LONG OS2API GpiPaintRegion(HPS a0, ULONG a1);

    OS2EXPORT LONG OS2API GpiPtInRegion(HPS a0, ULONG a1, PPOINTL a2);

    OS2EXPORT LONG OS2API GpiRectInRegion(HPS a0, ULONG a1, PRECTL a2);

    /* INCL_GPIMETAFILES */

    OS2EXPORT BOOL OS2API GpiDeleteMetaFile(ULONG handle);

    OS2EXPORT ULONG OS2API GpiLoadMetaFile(HAB hab, PSZ filename);

    OS2EXPORT LONG OS2API GpiPlayMetaFile(HPS ps, ULONG handle, LONG count, PLONG options,
                                          PLONG segments, LONG descriptionLength, PSZ description);

    OS2EXPORT BOOL OS2API GpiSetMetaFileBits(ULONG handle, LONG offset, LONG length, PVOID bytes);

    OS2EXPORT ULONG OS2API GpiCopyMetaFile(ULONG a0);

    OS2EXPORT BOOL OS2API GpiQueryMetaFileBits(ULONG a0, LONG a1, LONG a2, PBYTE a3);

    OS2EXPORT LONG OS2API GpiQueryMetaFileLength(ULONG a0);

    OS2EXPORT BOOL OS2API GpiSaveMetaFile(ULONG a0, PSZ a1);

    /* INCL_GPIDEFAULTS */

    OS2EXPORT BOOL OS2API GpiQueryDefViewingLimits(HPS a0, PRECTL a1);

    OS2EXPORT BOOL OS2API GpiSetDefViewingLimits(HPS a0, PRECTL a1);

    /* INCL_GPIPOLYGON */

    OS2EXPORT LONG OS2API GpiPolygons(HPS a0, ULONG a1, LXPOLYGON *a2, ULONG a3, ULONG a4);

#ifdef __cplusplus
}
#endif

#endif
