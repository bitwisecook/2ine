/* Public domain. No rights reserved. Wholly AI-authored. */
/* The generic GDI object calls: GetObject, DeleteObject, GetObjectType and GetStockObject. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/pmmerge/pmgre/graphics.h"

LONG GetObject(ULONG handle, LONG capacity, PVOID output)
{
    if (capacity < 0)
    {
        open32SetError(87);
        return 0;
    }
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    if (!bitmap)
    {
        LONG result = open32FontObject(handle, capacity, output);
        if (!result)
            result = open32PenObject(handle, capacity, output);
        if (!result)
            result = open32BrushObject(handle, capacity, output);
        if (!result)
            result = open32PaletteObject(handle, capacity, output);
        if (!result)
            open32SetError(6);
        return result;
    }
    if (!bitmap || capacity < 0 || (output && capacity < 24))
    {
        lx_handle_release(&borrow);
        open32SetError(bitmap ? 87 : 6);
        return 0;
    }
    if (output)
    {
        uint8 bytes[24] = {0};
        lx_guest_store_u32(bytes + 4, bitmap->width);
        lx_guest_store_u32(bytes + 8, bitmap->height);
        lx_guest_store_u32(bytes + 12, ((bitmap->width * bitmap->depth + 15) / 16) * 2);
        lx_guest_store_u16(bytes + 16, bitmap->planes);
        lx_guest_store_u16(bytes + 18, bitmap->depth);
        memcpy(output, bytes, sizeof bytes);
    }
    lx_handle_release(&borrow);
    return 24;
}

BOOL DeleteObject(ULONG handle)
{
    if (lx_pm_bitmap_delete(handle) || open32DeleteFont(handle) || open32DeleteBrush(handle) ||
        open32DeletePalette(handle) || open32DeleteRegion(handle) || open32DeletePen(handle))
        return TRUE;
    open32SetError(6);
    return FALSE;
}

ULONG GetObjectType(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (dc)
        return dc->kind == 1 ? 10 : 3;

    static const struct
    {
        LxHandleKind kind;
        ULONG type;
    } types[] = {{LX_HANDLE_OPEN32_PEN, 1},      {LX_HANDLE_OPEN32_BRUSH, 2},
                 {LX_HANDLE_OPEN32_METAFILE, 9}, {LX_HANDLE_OPEN32_ENHMETAFILE, 13},
                 {LX_HANDLE_OPEN32_PALETTE, 5},  {LX_HANDLE_OPEN32_FONT, 6},
                 {LX_HANDLE_PM_BITMAP, 7},       {LX_HANDLE_OPEN32_REGION, 8}};

    for (unsigned i = 0; i < sizeof types / sizeof types[0]; i++)
    {
        LxHandleBorrow borrow = {0};
        if (lx_handle_borrow(handle, types[i].kind, &borrow))
        {
            lx_handle_release(&borrow);
            return types[i].type;
        }
    }
    open32SetError(6);
    return 0;
}

ULONG GetStockObject(LONG index)
{
    if (index == DEFAULT_PALETTE)
        return open32StockPalette();
    if (index >= WHITE_PEN && index <= NULL_PEN)
        return open32StockPen(index);
    if (index >= WHITE_BRUSH && index <= NULL_BRUSH)
        return open32StockBrush(index);
    return open32StockFont(index);
}
