/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private graphics engine interfaces: PMMERGE's rasteriser and raster operations, which PMGPI,
   PMWINX and PMCTLS draw through. */

#ifndef _INCL_PMGREP_H_
#define _INCL_PMGREP_H_

#include "os2/include/os2def.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Scan conversion */

    typedef struct PmScanPoint
    {
        double x, y;
    } PmScanPoint;

    enum
    {
        /* Even-odd rule: GPI FPATH_ALTERNATE, Win32 ALTERNATE. */
        LX_PM_SCAN_ALTERNATE = 0,
        /* Non-zero rule: GPI FPATH_WINDING, Win32 WINDING. */
        LX_PM_SCAN_WINDING = 1,
        /* GPI inclusive fills (FPATH_INCL) also cover the top scan line and the right pel of each
           span; Win32 fills exclude both. */
        LX_PM_SCAN_INCLUSIVE = 2
    };

    /* Receives one span of pels [left, right) on row y, left < right. FALSE stops the scan. */
    typedef BOOL (*LxPmSpanSink)(void *context, LONG y, LONG left, LONG right);

    /* Reports, row by row from the lowest y, the pels whose centres lie inside the contours. The
       contours are consecutive runs of points (counts[i] points each) and close implicitly. A row
       y samples y + 0.5. Rows outside [first_row, end_row) are skipped. FALSE when memory runs out
       or the sink stops. */
    BOOL lx_pm_scan_polygons(const PmScanPoint *points, const size_t *counts, size_t contours,
                             unsigned rule, LONG first_row, LONG end_row, LxPmSpanSink sink,
                             void *context);

    /* Receives one pel of a line. FALSE stops the walk. */
    typedef BOOL (*LxPmPelVisitor)(void *context, LONG x, LONG y);

    /* Visits the pels of the Bresenham line from (x0, y0) towards (x1, y1). GPI lines include
       their end point; Win32 LineTo excludes it. FALSE when the visitor stopped the walk. */
    BOOL lx_pm_walk_line(LONG x0, LONG y0, LONG x1, LONG y1, BOOL include_end, LxPmPelVisitor visit,
                         void *context);

    /* Raster operations */

    /* Applies a ternary raster operation (GPI ROP_*, bits 16-23 of a Win32 one) to one pel: bit
       (pattern << 2 | source << 1 | destination) of rop gives each result bit. */
    uint32_t lx_pm_rop3(uint32_t pattern, uint32_t source, uint32_t destination, unsigned rop);

    /* Whether a raster operation's result depends on the source, or on the pattern. */
    static inline BOOL lx_pm_rop_uses_source(unsigned rop)
    {
        return ((rop ^ (rop >> 2)) & 0x33) != 0;
    }

    static inline BOOL lx_pm_rop_uses_pattern(unsigned rop)
    {
        return ((rop ^ (rop >> 4)) & 0x0f) != 0;
    }

    enum
    {
        /* The transfer ignores the destination; the pels start as 0. */
        LX_PM_BLIT_NO_READ = 0,
        /* The whole rectangle is read; the transfer fails when it cannot be. */
        LX_PM_BLIT_READ = 1,
        /* GPI: only the part inside the bit map or window is read; the rest starts as 0. */
        LX_PM_BLIT_READ_VISIBLE = 2
    };

    enum
    {
        LX_PM_PEL_KEEP,
        LX_PM_PEL_SET,
        LX_PM_PEL_FAIL
    };

    /* Computes one pel of a raster transfer at device (x, y). *pel holds the destination; the
       callback stores the result and returns LX_PM_PEL_SET, leaves it (LX_PM_PEL_KEEP), or stops
       the transfer (LX_PM_PEL_FAIL). */
    typedef int (*LxPmBlitPel)(void *context, LONG x, LONG y, uint32_t *pel);

    /* The read-modify-write raster transfer shared by GPI and Win32 blits. The device rectangle
       [xLeft, xRight) x [yBottom, yTop) is read as requested, every pel passes through pel from
       the bottom row up, and the rectangle is written back through the clip. FALSE when a read,
       allocation, write or pel fails; nothing is written then. */
    BOOL lx_pm_blit_rect(HPS ps, const RECTL *rect, unsigned read, LxPmBlitPel pel, void *context);

#ifdef __cplusplus
}
#endif

#endif
