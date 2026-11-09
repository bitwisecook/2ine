/* Public domain. No rights reserved. Wholly AI-authored. */
/* Brush objects: solid, null, hatched and pattern brushes, stock brushes, brush origins and
   the brush pel at a point. Colours are independent of DC text attributes. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/brushes.h"
#include "os2/include/private/pmgrep.h"

static void free_brush(void *resource)
{
    Open32Brush *brush = resource;
    free(brush->pixels);
    free(brush);
}

ULONG CreateSolidBrush(ULONG color)
{
    TRACE_NATIVE("CreateSolidBrush(%08X)", (uint)color);
    /* PALETTERGB is ordinary RGB on our true-color display. Retain the
       specifier in LOGBRUSH; raster conversion already uses its RGB bytes. */
    if ((color & 0xFF000000u) && (color & 0xFF000000u) != 0x02000000u)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    Open32Brush *brush = calloc(1, sizeof *brush);
    if (!brush)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    brush->color = color;
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_BRUSH, brush, free_brush);
    if (!handle)
    {
        free(brush);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    }
    return handle;
}

ULONG open32StockBrush(LONG index)
{
    static _Thread_local ULONG stocks[6];
    static const ULONG colors[] = {0xFFFFFF, 0xC0C0C0, 0x808080, 0x404040, 0, 0};
    if (index < 0 || index > 5)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
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

ULONG CreatePatternBrush(ULONG handle)
{
    TRACE_NATIVE("CreatePatternBrush(%u)", handle);
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    if (!bitmap)
    {
        open32SetError(ERROR_INVALID_HANDLE);
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
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    }
    return result;
}

BOOL open32BrushPixel(Open32DC *dc, LONG x, LONG y, uint32 *rgb)
{
    Open32Brush *brush = dc ? dc->brush.resource : NULL;
    if (!brush || brush->style == 1 || !rgb)
        return FALSE;
    if (brush->style == 2)
    {
        LONG px = (LONG)(((int64_t)x - dc->brush_origin_x) & 7),
             py = (LONG)(((int64_t)y - dc->brush_origin_y) & 7);
        BOOL ink = brush->bitmap == 0   ? py == 0
                   : brush->bitmap == 1 ? px == 0
                   : brush->bitmap == 2 ? ((px + py) & 7) == 0
                   : brush->bitmap == 3 ? ((px - py) & 7) == 0
                   : brush->bitmap == 4 ? (px == 0 || py == 0)
                                        : (((px + py) & 7) == 0 || ((px - py) & 7) == 0);
        if (!ink && dc->background_mode == 1)
            return FALSE;
        ULONG color = ink ? brush->color : dc->background_color;
        *rgb = (color & 255) << 16 | (color & 0xff00) | (color >> 16 & 255);
        return TRUE;
    }
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
        open32SetError(ERROR_INVALID_HANDLE);
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
        open32SetError(dc ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
        return FALSE;
    }
    lx_guest_store_u32(output, (ULONG)dc->brush_origin_x);
    lx_guest_store_u32((uint8 *)output + 4, (ULONG)dc->brush_origin_y);
    return TRUE;
}

ULONG CreateHatchBrush(LONG hatch, ULONG color)
{
    if (hatch < 0 || hatch > 5)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ULONG handle = CreateSolidBrush(color);
    LxHandleBorrow borrow = {0};
    if (handle && lx_handle_borrow(handle, LX_HANDLE_OPEN32_BRUSH, &borrow))
    {
        Open32Brush *brush = borrow.resource;
        brush->style = 2;
        brush->bitmap = (ULONG)hatch;
        lx_handle_release(&borrow);
    }
    return handle;
}

/* DIB pattern brushes decode into the same retained RGB tile as bitmap patterns. */
ULONG open32DibPatternBrush(PVOID information, PVOID bits, ULONG usage, BOOL monochrome)
{
    LONG width = (LONG)lx_guest_load_u32((uint8 *)information + 4),
         height = (LONG)lx_guest_load_u32((uint8 *)information + 8);
    LONG rows = height < 0 ? -height : height;
    ULONG dc = CreateCompatibleDC(0), result = 0;
    ULONG bitmap = dc ? CreateBitmap(width, rows, 1, 24, NULL) : 0;
    if (bitmap && SetDIBits(dc, bitmap, 0, (ULONG)rows, bits, information, usage) == rows)
        result = CreatePatternBrush(bitmap);
    LxHandleBorrow borrow = {0};
    if (result && monochrome && lx_handle_borrow(result, LX_HANDLE_OPEN32_BRUSH, &borrow))
    {
        ((Open32Brush *)borrow.resource)->depth = 1;
        lx_handle_release(&borrow);
    }
    if (bitmap)
        DeleteObject(bitmap);
    if (dc)
        DeleteDC(dc);
    return result;
}

/* A pattern tile as a packed bottom-up DIB, for metafile records. Monochrome tiles keep one bit
   per pel so the DC's text and background colors still apply on playback. */
uint8 *open32BrushDib(ULONG handle, size_t *information_bytes, size_t *bytes, BOOL *monochrome)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_BRUSH, &borrow))
        return NULL;
    Open32Brush *brush = borrow.resource;
    uint8 *dib = NULL;
    if (brush->style == 3)
    {
        BOOL mono = brush->depth == 1;
        size_t head = mono ? 48 : 40;
        size_t stride = mono ? ((size_t)brush->width + 31) / 32 * 4 : (size_t)brush->width * 4;
        dib = calloc(head + stride * brush->height, 1);
        if (dib)
        {
            lx_guest_store_u32(dib, 40);
            lx_guest_store_u32(dib + 4, brush->width);
            lx_guest_store_u32(dib + 8, brush->height);
            lx_guest_store_u16(dib + 12, 1);
            lx_guest_store_u16(dib + 14, mono ? 1 : 32);
            if (mono)
            {
                lx_guest_store_u32(dib + 32, 2);
                lx_guest_store_u32(dib + 44, 0xFFFFFF);
            }
            for (unsigned y = 0; y < brush->height; y++)
                for (unsigned x = 0; x < brush->width; x++)
                {
                    uint32 pixel = brush->pixels[(size_t)y * brush->width + x];
                    uint8 *row = dib + head + y * stride;
                    if (!mono)
                        lx_guest_store_u32(row + x * 4, pixel);
                    else if (pixel)
                        row[x / 8] |= (uint8)(0x80 >> (x % 8));
                }
            *information_bytes = head;
            *bytes = head + stride * brush->height;
            *monochrome = mono;
        }
    }
    lx_handle_release(&borrow);
    return dib;
}

static ULONG packed_pattern_brush(ULONG address, ULONG usage)
{
    uint8 header[40];
    size_t info_bytes = 0, pixel_bytes = 0, checked_info = 0, checked_pixels = 0;
    LxGuestBorrow dib = {0};
    if (!address || !lx_execution_read(GProcessState.execution, address, header, sizeof header) ||
        !open32DibBuffers(header, 0, UINT32_MAX, usage, &info_bytes, &pixel_bytes) ||
        !pixel_bytes ||
        !lx_execution_borrow(GProcessState.execution, address, info_bytes + pixel_bytes,
                             LX_GUEST_READ, &dib))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    uint8 *packed = malloc(info_bytes + pixel_bytes);
    if (packed)
        memcpy(packed, dib.host, info_bytes + pixel_bytes);
    lx_execution_release(&dib);
    ULONG result = 0;
    if (packed && open32DibBuffers(packed, 0, UINT32_MAX, usage, &checked_info, &checked_pixels) &&
        checked_info == info_bytes && checked_pixels == pixel_bytes)
        result = open32DibPatternBrush(packed, packed + info_bytes, usage, FALSE);
    else
        open32SetError(packed ? ERROR_INVALID_PARAMETER : ERROR_NOT_ENOUGH_MEMORY);
    free(packed);
    return result;
}

ULONG CreateBrushIndirect(PVOID input)
{
    if (!input)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ULONG style = lx_guest_load_u32(input), color = lx_guest_load_u32((uint8 *)input + 4),
          hatch = lx_guest_load_u32((uint8 *)input + 8);
    if (style == 0)
        return CreateSolidBrush(color);
    if (style == 2)
        return CreateHatchBrush((LONG)hatch, color);
    if (style == 3)
        return CreatePatternBrush(hatch);
    if (style == 6)
        return packed_pattern_brush(hatch, color & 0xFFFF);
    if (style == 5)
    {
        ULONG result = packed_pattern_brush(GlobalLock(hatch), color & 0xFFFF);
        GlobalUnlock(hatch);
        return result;
    }
    if (style == 1)
    {
        ULONG handle = CreateSolidBrush(0);
        LxHandleBorrow borrow = {0};
        if (handle && lx_handle_borrow(handle, LX_HANDLE_OPEN32_BRUSH, &borrow))
        {
            ((Open32Brush *)borrow.resource)->style = 1;
            lx_handle_release(&borrow);
        }
        return handle;
    }
    open32SetError(ERROR_INVALID_PARAMETER);
    return 0;
}
