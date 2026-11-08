/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_PMGPI_H_
#define _INCL_PMGPI_H_

#include "os2libs/os2types.h"
#include "os2libs/pmwin.h"
#include "os2libs/pmgpi_fonts.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        LONG cx, cy;
    } SIZEF;

    typedef SIZEF *PSIZEF;

    typedef struct RGNRECT
    {
        ULONG ircStart, crc, crcReturned, ulDirection;
    } RGNRECT;

    OS2EXPORT BOOL OS2API GpiQueryRegionRects(HPS ps, ULONG region, PRECTL bounds, RGNRECT *control,
                                              PRECTL rects) OS2APIINFO(587);
    OS2EXPORT LONG OS2API GpiCreateLogFont(HPS ps, PVOID name, LONG id, PFATTRS attrs)
        OS2APIINFO(368);
    OS2EXPORT BOOL OS2API GpiDeleteSetId(HPS ps, LONG id) OS2APIINFO(378);
    OS2EXPORT BOOL OS2API GpiSetCharSet(HPS ps, LONG id) OS2APIINFO(513);
    OS2EXPORT LONG OS2API GpiQueryCharSet(HPS ps) OS2APIINFO(433);
    OS2EXPORT BOOL OS2API GpiSetCharBox(HPS ps, PSIZEF box) OS2APIINFO(510);
    OS2EXPORT BOOL OS2API GpiSetCharAngle(HPS ps, PPOINTL angle) OS2APIINFO(509);
    OS2EXPORT BOOL OS2API GpiSetCharShear(HPS ps, PPOINTL shear) OS2APIINFO(514);
    OS2EXPORT BOOL OS2API GpiSetCharDirection(HPS ps, LONG direction) OS2APIINFO(511);
    OS2EXPORT LONG OS2API GpiQueryCharDirection(HPS ps) OS2APIINFO(431);
    OS2EXPORT BOOL OS2API GpiSetTextAlignment(HPS ps, LONG horizontal, LONG vertical)
        OS2APIINFO(649);
    OS2EXPORT BOOL OS2API GpiQueryTextAlignment(HPS ps, PLONG horizontal, PLONG vertical)
        OS2APIINFO(648);
    OS2EXPORT BOOL OS2API GpiQueryCharShear(HPS ps, PPOINTL shear) OS2APIINFO(434);
    OS2EXPORT BOOL OS2API GpiQueryCharAngle(HPS ps, PPOINTL angle) OS2APIINFO(429);
    OS2EXPORT BOOL OS2API GpiQueryCharBox(HPS ps, PSIZEF box) OS2APIINFO(430);
    OS2EXPORT BOOL OS2API GpiSetBackMix(HPS ps, LONG mode) OS2APIINFO(505);
    OS2EXPORT LONG OS2API GpiQueryBackMix(HPS ps) OS2APIINFO(425);
    OS2EXPORT BOOL OS2API GpiSetMix(HPS ps, LONG mode) OS2APIINFO(537);
    OS2EXPORT LONG OS2API GpiQueryMix(HPS ps) OS2APIINFO(467);
    OS2EXPORT BOOL OS2API GpiSetBackColor(HPS ps, LONG color) OS2APIINFO(504);
    OS2EXPORT LONG OS2API GpiQueryBackColor(HPS ps) OS2APIINFO(424);
    OS2EXPORT BOOL OS2API GpiSetLineType(HPS ps, LONG type) OS2APIINFO(530);
    OS2EXPORT LONG OS2API GpiQueryLineType(HPS ps) OS2APIINFO(459);
    OS2EXPORT LONG OS2API GpiSavePS(HPS ps) OS2APIINFO(501);
    OS2EXPORT BOOL OS2API GpiRestorePS(HPS ps, LONG id) OS2APIINFO(499);
    OS2EXPORT BOOL OS2API GpiConvert(HPS ps, LONG source, LONG target, LONG count, PPOINTL points)
        OS2APIINFO(364);
    OS2EXPORT BOOL OS2API GpiSetPageViewport(HPS ps, PRECTL rect) OS2APIINFO(540);
    OS2EXPORT BOOL OS2API GpiQueryPageViewport(HPS ps, PRECTL rect) OS2APIINFO(472);
    OS2EXPORT BOOL OS2API GpiSetAttrs(HPS ps, LONG primitive, ULONG mask, ULONG defaults,
                                      PVOID attrs) OS2APIINFO(588);
    OS2EXPORT BOOL OS2API GpiDeleteMetaFile(ULONG handle) OS2APIINFO(375);
    /* A native metafile object holding a copy of the given bytes. */
    OS2EXPORT ULONG lx_pm_metafile_create(const void *data, size_t length);
    OS2EXPORT ULONG OS2API GpiLoadMetaFile(HAB hab, PSZ filename) OS2APIINFO(401);
    OS2EXPORT LONG OS2API GpiPlayMetaFile(HPS ps, ULONG handle, LONG count, PLONG options,
                                          PLONG segments, LONG descriptionLength, PSZ description)
        OS2APIINFO(411);
    OS2EXPORT BOOL OS2API GpiQueryBoundaryData(HPS ps, PRECTL bounds) OS2APIINFO(428);
    OS2EXPORT BOOL OS2API GpiSetDefaultViewMatrix(HPS ps, LONG count, PVOID matrix, LONG options)
        OS2APIINFO(520);
    OS2EXPORT BOOL OS2API GpiSetDrawControl(HPS ps, LONG control, LONG value) OS2APIINFO(521);
    OS2EXPORT BOOL OS2API GpiSetMetaFileBits(ULONG handle, LONG offset, LONG length, PVOID bytes)
        OS2APIINFO(536);
    OS2EXPORT LONG OS2API GpiSetBitmapBits(HPS ps, LONG start, LONG scans, PVOID bits, PVOID info)
        OS2APIINFO(602);
    OS2EXPORT LONG OS2API GpiQueryBitmapBits(HPS ps, LONG start, LONG scans, PVOID bits, PVOID info)
        OS2APIINFO(599);
    OS2EXPORT LONG OS2API GpiDrawBits(HPS ps, PVOID bits, PVOID info, LONG count, PPOINTL points,
                                      LONG rop, ULONG options) OS2APIINFO(603);
    OS2EXPORT ULONG OS2API GpiCreateBitmap(HPS ps, PVOID header, ULONG options, PVOID bits,
                                           PVOID info) OS2APIINFO(598);
    OS2EXPORT ULONG OS2API GpiLoadBitmap(HPS ps, HMODULE module, ULONG id, LONG width, LONG height)
        OS2APIINFO(399);
    OS2EXPORT BOOL OS2API GpiDeleteBitmap(ULONG bitmap) OS2APIINFO(371);
    OS2EXPORT ULONG OS2API GpiSetBitmap(HPS ps, ULONG bitmap) OS2APIINFO(506);
    OS2EXPORT BOOL OS2API GpiQueryBitmapParameters(ULONG bitmap, PVOID data) OS2APIINFO(573);
    OS2EXPORT BOOL OS2API GpiQueryBitmapInfoHeader(ULONG bitmap, PVOID data) OS2APIINFO(601);
    OS2EXPORT BOOL OS2API GpiQueryDeviceBitmapFormats(HPS ps, LONG count, PLONG output)
        OS2APIINFO(445);
    OS2EXPORT LONG OS2API GpiBitBlt(HPS destination, HPS source, LONG count, PPOINTL points,
                                    LONG rop, ULONG options) OS2APIINFO(355);
    OS2EXPORT LONG OS2API GpiWCBitBlt(HPS destination, ULONG bitmap, LONG count, PPOINTL points,
                                      LONG rop, ULONG options) OS2APIINFO(557);
    OS2EXPORT BOOL OS2API GpiSetBitmapId(HPS ps, ULONG bitmap, LONG id) OS2APIINFO(508);
    OS2EXPORT ULONG OS2API GpiQueryBitmapHandle(HPS ps, LONG id) OS2APIINFO(427);
    OS2EXPORT LONG OS2API GpiQueryNumberSetIds(HPS ps) OS2APIINFO(470);
    OS2EXPORT BOOL OS2API GpiQuerySetIds(HPS ps, LONG count, PLONG types, PVOID names, PLONG ids)
        OS2APIINFO(486);
    OS2EXPORT BOOL OS2API GpiQueryWidthTable(HPS ps, LONG first, LONG count, PLONG output)
        OS2APIINFO(492);
    OS2EXPORT LONG OS2API GpiCharString(HPS ps, LONG count, PCH text) OS2APIINFO(358);
    OS2EXPORT LONG OS2API GpiCharStringAt(HPS ps, PPOINTL point, LONG count, PCH text)
        OS2APIINFO(359);
    OS2EXPORT LONG OS2API GpiQueryPatternSet(HPS ps) OS2APIINFO(475);
    OS2EXPORT BOOL OS2API GpiSetPatternRefPoint(HPS ps, PPOINTL point) OS2APIINFO(542);
    OS2EXPORT BOOL OS2API GpiQueryPatternRefPoint(HPS ps, PPOINTL point) OS2APIINFO(474);
    OS2EXPORT BOOL OS2API GpiSetPatternSet(HPS ps, LONG id) OS2APIINFO(543);
    OS2EXPORT LONG OS2API GpiQueryPattern(HPS ps) OS2APIINFO(473);
    OS2EXPORT BOOL OS2API GpiSetPattern(HPS ps, LONG symbol) OS2APIINFO(541);
    OS2EXPORT BOOL OS2API GpiQueryCurrentPosition(HPS ps, PPOINTL point) OS2APIINFO(441);
    OS2EXPORT BOOL OS2API GpiSetCurrentPosition(HPS ps, PPOINTL point) OS2APIINFO(519);
    OS2EXPORT ULONG OS2API GpiCreateRegion(HPS ps, LONG count, PRECTL rectangles) OS2APIINFO(370);
    OS2EXPORT BOOL OS2API GpiDestroyRegion(HPS ps, ULONG region) OS2APIINFO(611);
    OS2EXPORT LONG OS2API GpiCombineRegion(HPS ps, ULONG destination, ULONG first, ULONG second,
                                           LONG mode) OS2APIINFO(362);
    OS2EXPORT BOOL OS2API GpiSetRegion(HPS ps, ULONG region, LONG count, PRECTL rectangles)
        OS2APIINFO(546);
    OS2EXPORT LONG OS2API GpiQueryRegionBox(HPS ps, ULONG region, PRECTL rectangle) OS2APIINFO(481);
    OS2EXPORT LONG OS2API GpiSetClipRegion(HPS ps, ULONG region, PULONG old) OS2APIINFO(516);
    OS2EXPORT ULONG OS2API GpiQueryClipRegion(HPS ps) OS2APIINFO(436);
    OS2EXPORT LONG OS2API GpiOffsetRegion(HPS ps, ULONG region, PPOINTL point) OS2APIINFO(407);
    OS2EXPORT LONG OS2API GpiOffsetClipRegion(HPS ps, PPOINTL point) OS2APIINFO(405);
    OS2EXPORT LONG OS2API GpiRectVisible(HPS ps, PRECTL rect) OS2APIINFO(495);
    OS2EXPORT LONG OS2API GpiQueryClipBox(HPS ps, PRECTL rect) OS2APIINFO(435);
    OS2EXPORT BOOL OS2API GpiQueryColorData(HPS ps, LONG count, PLONG output) OS2APIINFO(438);
    OS2EXPORT LONG OS2API GpiIntersectClipRectangle(HPS ps, PRECTL rect) OS2APIINFO(396);
    OS2EXPORT LONG OS2API GpiCharStringPosAt(HPS ps, PPOINTL start, PRECTL rect, ULONG flags,
                                             LONG count, PCH text, PLONG advances) OS2APIINFO(581);
    OS2EXPORT LONG OS2API GpiCharStringPos(HPS ps, PRECTL rect, ULONG flags, LONG count, PCH text,
                                           PLONG advances) OS2APIINFO(580);
    OS2EXPORT LONG OS2API GpiBox(HPS ps, LONG control, PPOINTL point, LONG horizontal,
                                 LONG vertical) OS2APIINFO(356);

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
    OS2EXPORT LONG OS2API DevEscape(HDC dc, LONG code, LONG input_count, PVOID input,
                                    PLONG output_count, PVOID output) OS2APIINFO(605);
    OS2EXPORT LONG OS2API DevPostDeviceModes(HAB hab, PVOID data, PSZ driver, PSZ device, PSZ name,
                                             ULONG options) OS2APIINFO(609);
    OS2EXPORT BOOL OS2API DevQueryDeviceNames(HAB hab, PSZ driver, PLONG devices, PVOID names,
                                              PVOID descriptions, PLONG types, PVOID data_types)
        OS2APIINFO(607);
    OS2EXPORT LONG OS2API DevQueryHardcopyCaps(HDC dc, LONG first, LONG count, PVOID output)
        OS2APIINFO(608);
    OS2EXPORT HDC OS2API DevOpenDC(HAB hab, LONG type, PSZ token, LONG count, PVOID data,
                                   HDC compatible) OS2APIINFO(610);
    OS2EXPORT ULONG OS2API DevCloseDC(HDC dc) OS2APIINFO(604);
    OS2EXPORT HDC OS2API GpiQueryDevice(HPS ps) OS2APIINFO(444);
    OS2EXPORT BOOL OS2API GpiAssociate(HPS ps, HDC dc) OS2APIINFO(351);
    OS2EXPORT BOOL OS2API GpiSetCp(HPS ps, ULONG codepage) OS2APIINFO(518);
    OS2EXPORT ULONG OS2API GpiQueryCp(HPS ps) OS2APIINFO(440);
    OS2EXPORT BOOL OS2API GpiCreateLogColorTable(HPS ps, ULONG options, LONG format, LONG start,
                                                 LONG count, PLONG colors) OS2APIINFO(592);
    OS2EXPORT LONG OS2API GpiQueryFonts(HPS hps, ULONG options, PSZ face, PLONG requested,
                                        LONG record_size, PFONTMETRICS output) OS2APIINFO(586);
    OS2EXPORT BOOL OS2API GpiQueryFontMetrics(HPS hps, LONG record_size, PFONTMETRICS output)
        OS2APIINFO(453);
    OS2EXPORT LONG OS2API GpiQueryColorIndex(HPS hps, ULONG options, LONG rgb) OS2APIINFO(439);
    OS2EXPORT LONG OS2API GpiQueryRGBColor(HPS hps, ULONG options, LONG index) OS2APIINFO(479);
    OS2EXPORT BOOL OS2API GpiErase(HPS hps) OS2APIINFO(389);
    OS2EXPORT HPS OS2API GpiCreatePS(HAB hab, HDC dc, PSIZEL size, ULONG flags) OS2APIINFO(369);
    OS2EXPORT BOOL OS2API GpiDestroyPS(HPS hps) OS2APIINFO(379);
    OS2EXPORT LONG OS2API GpiPolyLine(HPS ps, LONG count, PPOINTL points) OS2APIINFO(415);
    OS2EXPORT LONG OS2API GpiLine(HPS hps, PPOINTL point) OS2APIINFO(398);
    OS2EXPORT BOOL OS2API GpiMove(HPS hps, PPOINTL point) OS2APIINFO(404);
    OS2EXPORT BOOL OS2API GpiSetColor(HPS hps, LONG color) OS2APIINFO(517);
    OS2EXPORT BOOL OS2API GpiSetDrawingMode(HPS hps, LONG mode) OS2APIINFO(522);
    OS2EXPORT LONG OS2API GpiSetPel(HPS hps, PPOINTL point) OS2APIINFO(544);
    OS2EXPORT BOOL OS2API GpiQueryTextBox(HPS hps, LONG lCount1, PCH pchString, LONG lCount2,
                                          PPOINTL aptlPoints) OS2APIINFO(489);

    OS2EXPORT LONG OS2API GpiQueryColor(HPS ps) OS2APIINFO(437);
    OS2EXPORT LONG OS2API GpiQueryLogColorTable(HPS ps, ULONG options, LONG start, LONG count,
                                                PLONG output) OS2APIINFO(593);
    OS2EXPORT BOOL OS2API GpiSetCharMode(HPS ps, LONG mode) OS2APIINFO(512);
    OS2EXPORT LONG OS2API GpiQueryCharMode(HPS ps) OS2APIINFO(432);

    typedef struct LXPOLYGON
    {
        ULONG count;
        PPOINTL points;
    } LXPOLYGON;

    OS2EXPORT BOOL OS2API GpiBeginArea(HPS a0, ULONG a1) OS2APIINFO(352);
    OS2EXPORT BOOL OS2API GpiBeginPath(HPS a0, LONG a1) OS2APIINFO(354);
    OS2EXPORT BOOL OS2API GpiCloseFigure(HPS a0) OS2APIINFO(360);
    OS2EXPORT ULONG OS2API GpiCopyMetaFile(ULONG a0) OS2APIINFO(365);
    OS2EXPORT LONG OS2API GpiEndArea(HPS a0) OS2APIINFO(385);
    OS2EXPORT BOOL OS2API GpiEndPath(HPS a0) OS2APIINFO(387);
    OS2EXPORT LONG OS2API GpiFillPath(HPS a0, LONG a1, LONG a2) OS2APIINFO(392);
    OS2EXPORT LONG OS2API GpiFullArc(HPS a0, LONG a1, LONG a2) OS2APIINFO(393);
    OS2EXPORT LONG OS2API GpiPaintRegion(HPS a0, ULONG a1) OS2APIINFO(409);
    OS2EXPORT LONG OS2API GpiPointArc(HPS a0, PPOINTL a1) OS2APIINFO(412);
    OS2EXPORT LONG OS2API GpiPtInRegion(HPS a0, ULONG a1, PPOINTL a2) OS2APIINFO(419);
    OS2EXPORT BOOL OS2API GpiQueryDefaultViewMatrix(HPS a0, LONG a1, PVOID a2) OS2APIINFO(443);
    OS2EXPORT BOOL OS2API GpiLoadPublicFonts(HAB a0, PSZ a1) OS2APIINFO(622);
    OS2EXPORT BOOL OS2API GpiUnloadPublicFonts(HAB a0, PSZ a1) OS2APIINFO(623);
    OS2EXPORT BOOL OS2API GpiQueryGraphicsField(HPS a0, PRECTL a1) OS2APIINFO(454);
    OS2EXPORT LONG OS2API GpiQueryLineEnd(HPS a0) OS2APIINFO(457);
    OS2EXPORT LONG OS2API GpiQueryLineJoin(HPS a0) OS2APIINFO(458);
    OS2EXPORT LONG OS2API GpiQueryLineWidthGeom(HPS a0) OS2APIINFO(461);
    OS2EXPORT BOOL OS2API GpiQueryMetaFileBits(ULONG a0, LONG a1, LONG a2, PBYTE a3)
        OS2APIINFO(465);
    OS2EXPORT LONG OS2API GpiQueryMetaFileLength(ULONG a0) OS2APIINFO(466);
    OS2EXPORT BOOL OS2API GpiQueryModelTransformMatrix(HPS a0, LONG a1, PVOID a2) OS2APIINFO(468);
    OS2EXPORT LONG OS2API GpiQueryNearestColor(HPS a0, ULONG a1, LONG a2) OS2APIINFO(469);
    OS2EXPORT ULONG OS2API GpiQueryPS(HPS a0, PSIZEL a1) OS2APIINFO(471);
    OS2EXPORT LONG OS2API GpiQueryPel(HPS a0, PPOINTL a1) OS2APIINFO(476);
    OS2EXPORT BOOL OS2API GpiQueryPickAperturePosition(HPS a0, PPOINTL a1) OS2APIINFO(477);
    OS2EXPORT BOOL OS2API GpiQueryPickApertureSize(HPS a0, PSIZEL a1) OS2APIINFO(478);
    OS2EXPORT LONG OS2API GpiQueryRealColors(HPS a0, ULONG a1, LONG a2, LONG a3, PLONG a4)
        OS2APIINFO(480);
    OS2EXPORT BOOL OS2API GpiQueryViewingLimits(HPS a0, PRECTL a1) OS2APIINFO(490);
    OS2EXPORT LONG OS2API GpiRectInRegion(HPS a0, ULONG a1, PRECTL a2) OS2APIINFO(494);
    OS2EXPORT BOOL OS2API GpiResetBoundaryData(HPS a0) OS2APIINFO(497);
    OS2EXPORT BOOL OS2API GpiResetPS(HPS a0, ULONG a1) OS2APIINFO(498);
    OS2EXPORT BOOL OS2API GpiSaveMetaFile(ULONG a0, PSZ a1) OS2APIINFO(500);
    OS2EXPORT BOOL OS2API GpiSetArcParams(HPS a0, PLONG a1) OS2APIINFO(502);
    OS2EXPORT BOOL OS2API GpiSetGraphicsField(HPS a0, PRECTL a1) OS2APIINFO(526);
    OS2EXPORT BOOL OS2API GpiSetLineEnd(HPS a0, LONG a1) OS2APIINFO(528);
    OS2EXPORT BOOL OS2API GpiSetLineJoin(HPS a0, LONG a1) OS2APIINFO(529);
    OS2EXPORT BOOL OS2API GpiSetLineWidthGeom(HPS a0, LONG a1) OS2APIINFO(532);
    OS2EXPORT BOOL OS2API GpiSetModelTransformMatrix(HPS a0, LONG a1, PVOID a2, LONG a3)
        OS2APIINFO(538);
    OS2EXPORT BOOL OS2API GpiSetPS(HPS a0, PSIZEL a1, ULONG a2) OS2APIINFO(539);
    OS2EXPORT BOOL OS2API GpiSetPickAperturePosition(HPS a0, PPOINTL a1) OS2APIINFO(545);
    OS2EXPORT BOOL OS2API GpiSetStopDraw(HPS a0, LONG a1) OS2APIINFO(550);
    OS2EXPORT LONG OS2API GpiQueryStopDraw(HPS a0) OS2APIINFO(487);
    OS2EXPORT BOOL OS2API GpiSetViewingLimits(HPS a0, PRECTL a1) OS2APIINFO(552);
    OS2EXPORT LONG OS2API GpiStrokePath(HPS a0, LONG a1, ULONG a2) OS2APIINFO(554);
    OS2EXPORT ULONG OS2API GpiPathToRegion(HPS a0, LONG a1, LONG a2) OS2APIINFO(559);
    OS2EXPORT BOOL OS2API GpiTranslate(HPS a0, PVOID a1, LONG a2, PPOINTL a3) OS2APIINFO(564);
    OS2EXPORT BOOL OS2API GpiScale(HPS a0, PVOID a1, LONG a2, PLONG a3, PPOINTL a4) OS2APIINFO(565);
    OS2EXPORT BOOL OS2API GpiRotate(HPS a0, PVOID a1, LONG a2, LONG a3, PPOINTL a4) OS2APIINFO(566);
    OS2EXPORT BOOL OS2API GpiQueryDefViewingLimits(HPS a0, PRECTL a1) OS2APIINFO(569);
    OS2EXPORT BOOL OS2API GpiSetDefViewingLimits(HPS a0, PRECTL a1) OS2APIINFO(572);
    OS2EXPORT BOOL OS2API GpiDeletePalette(ULONG a0) OS2APIINFO(577);
    OS2EXPORT ULONG OS2API GpiSelectPalette(HPS a0, ULONG a1) OS2APIINFO(578);
    OS2EXPORT ULONG OS2API GpiQueryPalette(HPS a0) OS2APIINFO(579);
    OS2EXPORT LONG OS2API GpiQueryAttrs(HPS a0, LONG a1, ULONG a2, PVOID a3) OS2APIINFO(583);
    OS2EXPORT BOOL OS2API GpiSetPickApertureSize(HPS a0, LONG a1, PSIZEL a2) OS2APIINFO(589);
    OS2EXPORT ULONG OS2API GpiCreatePalette(HAB a0, ULONG a1, ULONG a2, ULONG a3, PULONG a4)
        OS2APIINFO(594);
    OS2EXPORT LONG OS2API GpiQueryPaletteInfo(ULONG a0, HPS a1, ULONG a2, ULONG a3, ULONG a4,
                                              PULONG a5) OS2APIINFO(597);
    OS2EXPORT LONG OS2API GpiPartialArc(HPS a0, PPOINTL a1, LONG a2, LONG a3, LONG a4)
        OS2APIINFO(612);
    OS2EXPORT BOOL OS2API GpiSetCharExtra(HPS a0, LONG a1) OS2APIINFO(614);
    OS2EXPORT LONG OS2API GpiPolygons(HPS a0, ULONG a1, LXPOLYGON *a2, ULONG a3, ULONG a4)
        OS2APIINFO(650);
#ifdef __cplusplus
}
#endif

#endif
