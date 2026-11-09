/* Public domain. No rights reserved. Wholly AI-authored. */
/* WinGetSysBitmap: the system bitmaps, including the check box image strip. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmgre/graphics.h"
#include "emulator/abi.h"

static ULONG checkbox_bitmap(void)
{
    /* SBMP_CHECKBOXES is a four-column, three-row atlas. The top row's
       first and last cells are the normal unchecked and checked boxes. */
    const unsigned cell = 13, width = cell * 4, height = cell * 3;
    ULONG handle = lx_pm_bitmap_create(width, height, 1, 24);
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    if (!bitmap)
        return 0;
    for (unsigned y = 0; y < height; y++)
        for (unsigned x = 0; x < width; x++)
        {
            unsigned column = x / cell, row = y / cell, px = x % cell, py = y % cell;
            BOOL pressed = column == 1 || column == 2, checked = column >= 2;
            uint32 color = 0xcccccc;
            if (row == 1)
            {
                int dx = (int)px - 6, dy = (int)py - 6, radius = dx * dx + dy * dy;
                if (radius <= 25)
                    color = radius >= 16 ? 0x404040 : checked && radius <= 4 ? 0 : 0xffffff;
            }
            else if (px >= 1 && px <= 11 && py >= 1 && py <= 11)
            {
                color = px == 1 || py == 11   ? 0x404040
                        : px == 11 || py == 1 ? 0xffffff
                        : pressed             ? 0xcccccc
                                              : 0xffffff;
                if (checked && px >= 3 && px <= 9 && py >= 3 && py <= 9)
                {
                    if (row == 0 || (px <= 5 && (py == 7 - px || py == 8 - px)) ||
                        (px >= 5 && (py == px - 3 || py == px - 2)))
                        color = 0;
                }
            }
            bitmap->pixels[(size_t)y * width + x] = color;
        }
    lx_handle_release(&borrow);
    return handle;
}

ULONG WinGetSysBitmap(HWND desktop, ULONG id)
{
    TRACE_NATIVE("WinGetSysBitmap(%u, %u)", (uint)desktop, (uint)id);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return 0;
    if (desktop != 1 || (id != 6 && id != 18 && id != 19 && id != 48))
    {
        lxHostThreadState()->pm_error = desktop != 1 ? PMERR_INVALID_HWND : PMERR_INVALID_PARM;
        return 0;
    }
    if (id == 48)
        return checkbox_bitmap();
    ULONG handle = lx_pm_bitmap_create(9, 9, 1, 1);
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    if (!bitmap)
        return 0;
    for (unsigned y = 0; y < 9; y++)
        for (unsigned x = 0; x < 9; x++)
        {
            int marked;
            if (id == 6)
                marked = (x >= 1 && x <= 3 && y == 5 - x) || (x >= 3 && x <= 7 && y == x - 1);
            else
                marked = x == 0 || y == 0 || x == 8 || y == 8 || (y == 4 && x >= 2 && x <= 6) ||
                         (id == 18 && x == 4 && y >= 2 && y <= 6);
            bitmap->pixels[y * 9 + x] = marked ? 0 : 0xffffff;
        }
    lx_handle_release(&borrow);
    return handle;
}
