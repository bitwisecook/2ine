/* Public domain. No rights reserved. Wholly AI-authored. */
/* Boxes preserve current position and include their boundary unless DRO_EXCLUSIVE is set. */
#include "os2/include/private/export.h"
#include "os2/include/pmgpi.h"
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/ps/pmgpi/internal.h"

LONG GpiBox(HPS handle, LONG control, PPOINTL point, LONG horizontal, LONG vertical)
{
    TRACE_NATIVE("GpiBox(%u, %d, %d,%d)", (uint)handle, (int)control, point ? (int)point->x : 0,
                 point ? (int)point->y : 0);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !point || horizontal < 0 || vertical < 0 ||
        (control & ~(DRO_OUTLINEFILL | DRO_EXCLUSIVE)) || !(control & DRO_OUTLINEFILL))
        return GPI_ERROR;
    POINTL from = ps->position, end = *point;
    if (!lx_pm_transform_point(ps, &from, TRUE) || !lx_pm_transform_point(ps, &end, TRUE))
        return GPI_ERROR;
    point = &end;
    RECTL rect = {from.x < point->x ? from.x : point->x, from.y < point->y ? from.y : point->y,
                  from.x > point->x ? from.x : point->x, from.y > point->y ? from.y : point->y};
    if (!(control & DRO_EXCLUSIVE))
    {
        if (rect.xRight == INT32_MAX || rect.yTop == INT32_MAX)
            return GPI_ERROR;
        rect.xRight++;
        rect.yTop++;
    }
    int64_t width = (int64_t)rect.xRight - rect.xLeft, height = (int64_t)rect.yTop - rect.yBottom;
    if (width <= 0 || height <= 0)
        return GPI_OK;
    if (width > LX_PM_RASTER_MAX_DIMENSION || height > LX_PM_RASTER_MAX_DIMENSION)
        return GPI_ERROR;
    if (horizontal > width)
        horizontal = (LONG)width;
    if (vertical > height)
        vertical = (LONG)height;
    LONG rx = horizontal / 2, ry = vertical / 2;
    BOOL fill = (control & DRO_FILL) != 0;
    for (LONG row = 0; row < height; row++)
    {
        LONG inset = 0;
        if (rx && ry && (row < ry || row >= height - ry))
        {
            int64_t dy = row < ry ? ry - row - 1 : row - (height - ry);
            while (inset < rx)
            {
                int64_t dx = rx - inset - 1;
                if (dx * dx * ry * ry + dy * dy * rx * rx <= ((int64_t)rx * rx * ry * ry))
                    break;
                inset++;
            }
        }
        LONG left = rect.xLeft + inset, right = rect.xRight - inset, y = rect.yBottom + row;
        if (fill && !lx_pm_fill_span(handle, left, y, right - left, GPI_PRIM_INDEX(PRIM_AREA)))
            return GPI_ERROR;
        if ((control & DRO_OUTLINE) && ps->line_type != LINETYPE_INVISIBLE)
        {
            if (row == 0 || row == height - 1)
            {
                if (!lx_pm_fill_span(handle, left, y, right - left, GPI_PRIM_INDEX(PRIM_LINE)))
                    return GPI_ERROR;
            }
            else if (!lx_pm_fill_span(handle, left, y, 1, GPI_PRIM_INDEX(PRIM_LINE)) ||
                     !lx_pm_fill_span(handle, right - 1, y, 1, GPI_PRIM_INDEX(PRIM_LINE)))
                return GPI_ERROR;
        }
    }
    return lx_pm_hits(ps);
}
