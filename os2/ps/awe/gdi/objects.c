/* Public domain. No rights reserved. Wholly AI-authored. */
/* The generic GDI object calls: GetObject, DeleteObject, GetObjectType and GetStockObject. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/dc.h"
#include "os2/include/private/pmgrep.h"

enum
{
    /* BITMAP rows are padded to whole 16-bit words. */
    BITMAP_ROW_ALIGN_BITS = 16,
    BITMAP_ROW_ALIGN_BYTES = 2
};

LONG GetObject(ULONG handle, LONG capacity, PVOID output)
{
    if (capacity < 0)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
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
            open32SetError(ERROR_INVALID_HANDLE);
        return result;
    }
    if (!bitmap || capacity < 0 || (output && capacity < WIN32_BITMAP_SIZE))
    {
        lx_handle_release(&borrow);
        open32SetError(bitmap ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
        return 0;
    }
    if (output)
    {
        uint8 bytes[WIN32_BITMAP_SIZE] = {0};
        lx_guest_store_u32(bytes + WIN32_BITMAP_WIDTH, bitmap->width);
        lx_guest_store_u32(bytes + WIN32_BITMAP_HEIGHT, bitmap->height);
        lx_guest_store_u32(
            bytes + WIN32_BITMAP_WIDTH_BYTES,
            ((bitmap->width * bitmap->depth + BITMAP_ROW_ALIGN_BITS - 1) / BITMAP_ROW_ALIGN_BITS) *
                BITMAP_ROW_ALIGN_BYTES);
        lx_guest_store_u16(bytes + WIN32_BITMAP_PLANES, bitmap->planes);
        lx_guest_store_u16(bytes + WIN32_BITMAP_BITS_PIXEL, bitmap->depth);
        memcpy(output, bytes, sizeof bytes);
    }
    lx_handle_release(&borrow);
    return WIN32_BITMAP_SIZE;
}

BOOL DeleteObject(ULONG handle)
{
    if (lx_pm_bitmap_delete(handle) || open32DeleteFont(handle) || open32DeleteBrush(handle) ||
        open32DeletePalette(handle) || open32DeleteRegion(handle) || open32DeletePen(handle))
        return TRUE;
    open32SetError(ERROR_INVALID_HANDLE);
    return FALSE;
}

ULONG GetObjectType(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (dc)
        return dc->kind == OPEN32_DC_MEMORY ? WIN32_OBJ_MEMDC : WIN32_OBJ_DC;

    static const struct
    {
        LxHandleKind kind;
        ULONG type;
    } types[] = {{LX_HANDLE_OPEN32_PEN, WIN32_OBJ_PEN},
                 {LX_HANDLE_OPEN32_BRUSH, WIN32_OBJ_BRUSH},
                 {LX_HANDLE_OPEN32_METAFILE, WIN32_OBJ_METAFILE},
                 {LX_HANDLE_OPEN32_ENHMETAFILE, WIN32_OBJ_ENHMETAFILE},
                 {LX_HANDLE_OPEN32_PALETTE, WIN32_OBJ_PAL},
                 {LX_HANDLE_OPEN32_FONT, WIN32_OBJ_FONT},
                 {LX_HANDLE_PM_BITMAP, WIN32_OBJ_BITMAP},
                 {LX_HANDLE_OPEN32_REGION, WIN32_OBJ_REGION}};

    for (unsigned i = 0; i < sizeof types / sizeof types[0]; i++)
    {
        LxHandleBorrow borrow = {0};
        if (lx_handle_borrow(handle, types[i].kind, &borrow))
        {
            lx_handle_release(&borrow);
            return types[i].type;
        }
    }
    open32SetError(ERROR_INVALID_HANDLE);
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
