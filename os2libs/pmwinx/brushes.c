/* Public domain. No rights reserved. Wholly AI-authored. */
/* Solid and null brushes retain RGB colors independently of DC text attributes. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/graphics.h"
typedef struct Open32Brush
{
    ULONG color, style;
    ULONG bitmap, width, height, depth;
    uint32 *pixels;
    BOOL stock;
} Open32Brush;
static BOOL brush_blt(ULONG handle, LONG x, LONG y, LONG width, LONG height, ULONG operation,
                      Open32Brush *brush);
static void free_brush(void *resource)
{
    Open32Brush *brush = resource;
    free(brush->pixels);
    free(brush);
}
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
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_BRUSH, brush, free_brush);
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
        lx_guest_store_u32((uint8 *)output + 8, brush->bitmap);
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
        if (style == 3)
        {
            if ((int64_t)right - left <= INT32_MAX && (int64_t)bottom - top <= INT32_MAX)
                result = brush_blt(handle, left, top, right - left, bottom - top, 0xf00021,
                                   borrow.resource);
        }
        else
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

static BOOL brush_blt(ULONG handle, LONG x, LONG y, LONG width, LONG height, ULONG operation,
                      Open32Brush *brush)
{
    TRACE_NATIVE("PatBlt(%u, %d, %d, %d, %d, %X)", handle, x, y, width, height, operation);
    Open32DC *dc = open32FindDC(handle);
    unsigned rop = (operation >> 16) & 255;
    if (!dc || !open32CanDraw(handle))
    {
        open32SetError(6);
        return FALSE;
    }
    if ((rop & 0x33) != ((rop >> 2) & 0x33))
    {
        open32SetError(87);
        return FALSE;
    }
    BOOL pattern = (rop & 15) != (rop >> 4);
    if (pattern && !brush)
    {
        open32SetError(6);
        return FALSE;
    }
    if (!width || !height || (pattern && brush->style == 1))
        return TRUE;
    int64_t end_x = (int64_t)x + width, end_y = (int64_t)y + height;
    int64_t left = x < end_x ? x : end_x, right = x < end_x ? end_x : x;
    int64_t top = y < end_y ? y : end_y, bottom = y < end_y ? end_y : y;
    LONG canvas_width = open32DCWidth(handle), canvas_height = open32DCHeight(handle);
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right > canvas_width)
        right = canvas_width;
    if (bottom > canvas_height)
        bottom = canvas_height;
    if (left >= right || top >= bottom)
        return TRUE;
    unsigned w = (unsigned)(right - left), h = (unsigned)(bottom - top);
    size_t count = (size_t)w * h;
    if (count > 16777216)
    {
        open32SetError(8);
        return FALSE;
    }
    uint32 *pixels = malloc(count * sizeof *pixels);
    if (!pixels)
    {
        open32SetError(8);
        return FALSE;
    }
    BOOL result = lx_pm_raster_read(handle, (LONG)left, canvas_height - (LONG)bottom, w, h, pixels);
    ULONG color = brush ? brush->color : 0;
    uint32 p = (color & 255) << 16 | (color & 0xff00) | (color >> 16 & 255);
    if (result)
    {
        for (size_t i = 0; i < count; i++)
        {
            if (pattern && brush->style == 3)
            {
                LONG px = (LONG)((left + (int64_t)(i % w) - dc->brush_origin_x) % brush->width);
                LONG py =
                    (LONG)((bottom - 1 - (int64_t)(i / w) - dc->brush_origin_y) % brush->height);
                if (px < 0)
                    px += (LONG)brush->width;
                if (py < 0)
                    py += (LONG)brush->height;
                p = brush->pixels[(size_t)(brush->height - 1 - py) * brush->width + px];
                if (brush->depth == 1)
                {
                    ULONG expanded = p ? dc->background_color : dc->text_color;
                    p = (expanded & 255) << 16 | (expanded & 0xff00) | (expanded >> 16 & 255);
                }
            }
            uint32 d = pixels[i], value = 0;
            if (rop & 1)
                value |= ~p & ~d;
            if (rop & 2)
                value |= ~p & d;
            if (rop & 16)
                value |= p & ~d;
            if (rop & 32)
                value |= p & d;
            pixels[i] = value & 0xffffff;
        }
        result = lx_pm_raster_write(handle, (LONG)left, canvas_height - (LONG)bottom, w, h, pixels);
    }
    free(pixels);
    return result;
}

BOOL PatBlt(ULONG handle, LONG x, LONG y, LONG width, LONG height, ULONG operation)
{
    Open32DC *dc = open32FindDC(handle);
    return brush_blt(handle, x, y, width, height, operation, dc ? dc->brush.resource : NULL);
}
ULONG CreatePatternBrush(ULONG handle)
{
    TRACE_NATIVE("CreatePatternBrush(%u)", handle);
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    if (!bitmap)
    {
        open32SetError(6);
        return 0;
    }
    Open32Brush *brush = calloc(1, sizeof *brush);
    size_t bytes = (size_t)bitmap->width * bitmap->height * sizeof *bitmap->pixels;
    if (brush)
        brush->pixels = malloc(bytes);
    if (!brush || !brush->pixels)
    {
        free(brush);
        lx_handle_release(&borrow);
        open32SetError(8);
        return 0;
    }
    brush->style = 3;
    brush->bitmap = handle;
    brush->width = bitmap->width;
    brush->height = bitmap->height;
    brush->depth = bitmap->depth;
    memcpy(brush->pixels, bitmap->pixels, bytes);
    if (brush->depth == 1 && bitmap->palette_count >= 2)
        for (size_t i = 0; i < bytes / sizeof *brush->pixels; i++)
            brush->pixels[i] = brush->pixels[i] == bitmap->palette[1] ? 0xffffff : 0;
    lx_handle_release(&borrow);
    ULONG result = lx_handle_create(LX_HANDLE_OPEN32_BRUSH, brush, free_brush);
    if (!result)
    {
        free_brush(brush);
        open32SetError(8);
    }
    return result;
}

BOOL open32BrushPixel(Open32DC *dc, LONG x, LONG y, uint32 *rgb)
{
    Open32Brush *brush = dc ? dc->brush.resource : NULL;
    if (!brush || brush->style == 1 || !rgb)
        return FALSE;
    if (brush->style == 3)
    {
        LONG tx = (LONG)(((int64_t)x - dc->brush_origin_x) % brush->width);
        LONG ty = (LONG)(((int64_t)y - dc->brush_origin_y) % brush->height);
        if (tx < 0)
            tx += (LONG)brush->width;
        if (ty < 0)
            ty += (LONG)brush->height;
        uint32 pixel =
            brush->pixels[(size_t)(brush->height - 1 - (unsigned)ty) * brush->width + (unsigned)tx];
        if (brush->depth != 1)
        {
            *rgb = pixel;
            return TRUE;
        }
        ULONG color = pixel ? dc->background_color : dc->text_color;
        *rgb = (color & 255) << 16 | (color & 0xff00) | (color >> 16 & 255);
        return TRUE;
    }
    ULONG color = brush->color;
    *rgb = (color & 255) << 16 | (color & 0xff00) | (color >> 16 & 255);
    return TRUE;
}

BOOL SetBrushOrgEx(ULONG handle, LONG x, LONG y, PVOID previous)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc)
    {
        open32SetError(6);
        return FALSE;
    }
    if (previous)
    {
        lx_guest_store_u32(previous, (ULONG)dc->brush_origin_x);
        lx_guest_store_u32((uint8 *)previous + 4, (ULONG)dc->brush_origin_y);
    }
    dc->brush_origin_x = x;
    dc->brush_origin_y = y;
    return TRUE;
}
BOOL GetBrushOrgEx(ULONG handle, PVOID output)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !output)
    {
        open32SetError(dc ? 87 : 6);
        return FALSE;
    }
    lx_guest_store_u32(output, (ULONG)dc->brush_origin_x);
    lx_guest_store_u32((uint8 *)output + 4, (ULONG)dc->brush_origin_y);
    return TRUE;
}
