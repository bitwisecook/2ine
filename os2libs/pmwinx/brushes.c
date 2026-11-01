/* Public domain. No rights reserved. Wholly AI-authored. */
/* Solid and null brushes retain RGB colors independently of DC text attributes. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/graphics.h"
typedef struct Open32Brush
{
    ULONG color, style;
    BOOL stock;
} Open32Brush;
ULONG CreateSolidBrush(ULONG color)
{
    if (color & 0xFF000000)
    {
        open32SetError(87);
        return 0;
    }
    Open32Brush *brush = calloc(1, sizeof *brush);
    if (!brush)
    {
        open32SetError(8);
        return 0;
    }
    brush->color = color;
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_BRUSH, brush, free);
    if (!handle)
    {
        free(brush);
        open32SetError(8);
    }
    return handle;
}
ULONG open32StockBrush(LONG index)
{
    static _Thread_local ULONG stocks[6];
    static const ULONG colors[] = {0xFFFFFF, 0xC0C0C0, 0x808080, 0x404040, 0, 0};
    if (index < 0 || index > 5)
    {
        open32SetError(87);
        return 0;
    }
    if (!stocks[index])
    {
        ULONG handle = CreateSolidBrush(colors[index]);
        LxHandleBorrow borrow = {0};
        if (!handle || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_BRUSH, &borrow))
            return 0;
        Open32Brush *brush = borrow.resource;
        brush->stock = TRUE;
        brush->style = index == 5;
        lx_handle_release(&borrow);
        stocks[index] = handle;
    }
    return stocks[index];
}
ULONG open32SelectBrush(Open32DC *dc, ULONG handle)
{
    LxHandleBorrow next = {0};
    if (!dc || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_BRUSH, &next))
        return 0;
    ULONG previous = dc->brush_handle;
    lx_handle_release(&dc->brush);
    dc->brush = next;
    dc->brush_handle = handle;
    return previous;
}
BOOL open32DeleteBrush(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_BRUSH, &borrow))
        return FALSE;
    Open32Brush *brush = borrow.resource;
    BOOL result =
        !brush->stock && lx_handle_close_guarded(handle, LX_HANDLE_OPEN32_BRUSH, 1, NULL) == 1;
    lx_handle_release(&borrow);
    return result;
}
LONG open32BrushObject(ULONG handle, LONG capacity, PVOID output)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_BRUSH, &borrow))
        return 0;
    Open32Brush *brush = borrow.resource;
    LONG result = !output ? 12 : capacity >= 12 ? 12 : 0;
    if (output && result)
    {
        lx_guest_store_u32(output, brush->style);
        lx_guest_store_u32((uint8 *)output + 4, brush->color);
        lx_guest_store_u32((uint8 *)output + 8, 0);
    }
    lx_handle_release(&borrow);
    return result;
}
LONG FillRect(ULONG handle, PVOID rectangle, ULONG brush_handle)
{
    if (!open32CanDraw(handle) || !rectangle)
    {
        open32SetError(87);
        return 0;
    }
    LxHandleBorrow borrow = {0};
    ULONG color, style = 0;
    if (brush_handle > 0 && brush_handle <= 21)
        color = GetSysColor((LONG)brush_handle - 1);
    else if (lx_handle_borrow(brush_handle, LX_HANDLE_OPEN32_BRUSH, &borrow))
    {
        Open32Brush *brush = borrow.resource;
        color = brush->color;
        style = brush->style;
    }
    else
    {
        open32SetError(6);
        return 0;
    }
    const uint8 *wire = rectangle;
    LONG left = (LONG)lx_guest_load_u32(wire), top = (LONG)lx_guest_load_u32(wire + 4);
    LONG right = (LONG)lx_guest_load_u32(wire + 8), bottom = (LONG)lx_guest_load_u32(wire + 12);
    int64_t lower = (int64_t)open32DCHeight(handle) - bottom,
            upper = (int64_t)open32DCHeight(handle) - top;
    BOOL result = FALSE;
    if (lower >= INT32_MIN && upper <= INT32_MAX && lower <= upper && left <= right)
    {
        RECTL rect = {left, (LONG)lower, right, (LONG)upper};
        LONG rgb = (LONG)((color & 255) << 16 | (color & 0xFF00) | (color >> 16 & 255));
        result = style == 1 || WinFillRect(handle, &rect, rgb);
    }
    lx_handle_release(&borrow);
    if (!result)
        open32SetError(87);
    return result;
}

LONG FrameRect(ULONG handle, PVOID input, ULONG brush)
{
    if (!input || !open32CanDraw(handle))
    {
        open32SetError(87);
        return 0;
    }
    const uint8 *wire = input;
    LONG left = (LONG)lx_guest_load_u32(wire), top = (LONG)lx_guest_load_u32(wire + 4);
    LONG right = (LONG)lx_guest_load_u32(wire + 8), bottom = (LONG)lx_guest_load_u32(wire + 12);
    if (left >= right || top >= bottom)
        return 0;
    const LONG edges[4][4] = {{left, top, right, top + 1},
                              {left, bottom - 1, right, bottom},
                              {left, top, left + 1, bottom},
                              {right - 1, top, right, bottom}};
    for (unsigned edge = 0; edge < 4; edge++)
    {
        uint8 rectangle[16];
        for (unsigned i = 0; i < 4; i++)
            lx_guest_store_u32(rectangle + i * 4, (ULONG)edges[edge][i]);
        if (!FillRect(handle, rectangle, brush))
            return 0;
    }
    return 1;
}
BOOL InvertRect(ULONG handle, PVOID input)
{
    if (!input || !open32CanDraw(handle))
    {
        open32SetError(87);
        return FALSE;
    }
    LONG height = open32DCHeight(handle);
    LONG left = (LONG)lx_guest_load_u32(input), right = (LONG)lx_guest_load_u32((uint8 *)input + 8);
    int64_t top = (int64_t)height - (LONG)lx_guest_load_u32((uint8 *)input + 4);
    int64_t bottom = (int64_t)height - (LONG)lx_guest_load_u32((uint8 *)input + 12);
    if (top < INT32_MIN || top > INT32_MAX || bottom < INT32_MIN || bottom > INT32_MAX)
    {
        open32SetError(87);
        return FALSE;
    }
    RECTL rectangle = {left, (LONG)bottom, right, (LONG)top};
    return WinInvertRect(handle, &rectangle);
}
