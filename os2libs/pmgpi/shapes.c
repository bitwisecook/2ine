/* Public domain. No rights reserved. Wholly AI-authored. */
/* Boxes preserve current position and include their boundary unless DRO_EXCLUSIVE is set. */
#include "os2libs/os2native.h"
#include "os2libs/pmgpi.h"
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
LONG GpiBox(HPS handle, LONG control, PPOINTL point, LONG horizontal, LONG vertical)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !point || horizontal < 0 || vertical < 0 || (control & ~0x10000003) ||
        !(control & 3))
        return 0;
    POINTL from = ps->position;
    RECTL rect = {from.x < point->x ? from.x : point->x, from.y < point->y ? from.y : point->y,
                  from.x > point->x ? from.x : point->x, from.y > point->y ? from.y : point->y};
    if (!(control & 0x10000000))
    {
        if (rect.xRight == INT32_MAX || rect.yTop == INT32_MAX)
            return 0;
        rect.xRight++;
        rect.yTop++;
    }
    int64_t width = (int64_t)rect.xRight - rect.xLeft, height = (int64_t)rect.yTop - rect.yBottom;
    if (width <= 0 || height <= 0)
        return 1;
    if (width > 16384 || height > 16384)
        return 0;
    if (horizontal > width)
        horizontal = (LONG)width;
    if (vertical > height)
        vertical = (LONG)height;
    LONG rx = horizontal / 2, ry = vertical / 2;
    BOOL fill = (control & 1) && ps->pattern != 15 && ps->pattern != 64;
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
        if (fill || ((control & 2) && (row == 0 || row == height - 1)))
        {
            RECTL span = {left, y, right, y + 1};
            if (!WinFillRect(handle, &span, ps->primitive_color[3]))
                return 0;
        }
        else if (control & 2)
        {
            RECTL span = {left, y, left + 1, y + 1};
            if (!WinFillRect(handle, &span, ps->primitive_color[3]))
                return 0;
            span = (RECTL){right - 1, y, right, y + 1};
            if (!WinFillRect(handle, &span, ps->primitive_color[3]))
                return 0;
        }
    }
    return 1;
}
