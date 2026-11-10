/* Public domain. No rights reserved. Wholly AI-authored. */
/* Brush objects: solid, null, hatched and pattern brushes, stock brushes, brush origins and
   the brush pel at a point. Colours are independent of DC text attributes. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/brushes.h"
#include "os2/include/private/pmgrep.h"

enum
{
    /* Hatch brushes repeat an 8-by-8 pel cell. */
    HATCH_CELL_MASK = 7,
    /* A DIB pattern LOGBRUSH carries its DIB_RGB_COLORS/DIB_PAL_COLORS usage in lbColor's
       low word. */
    DIB_USAGE_MASK = 0xFFFF,
    /* Pattern tiles become 32-bit or monochrome DIBs with rows padded to 32 bits. */
    PATTERN_TILE_BITS = 32,
    PATTERN_TILE_BYTES_PER_PEL = 4,
    TRUE_COLOR_BITS = 24,
    DIB_ROW_ALIGN_BITS = 32,
    DIB_ROW_ALIGN_BYTES = 4,
    MONOCHROME_COLORS = 2
};

/* The high byte of a COLORREF is 0 for an RGB value or 2 for PALETTERGB. */
enum
{
    COLORREF_FLAGS_MASK = 0xFF000000u,
    COLORREF_PALETTE_RGB = 0x02000000u
};

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
    if ((color & COLORREF_FLAGS_MASK) && (color & COLORREF_FLAGS_MASK) != COLORREF_PALETTE_RGB)
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
    static _Thread_local ULONG stocks[NULL_BRUSH + 1];
    /* WHITE_BRUSH, LTGRAY_BRUSH, GRAY_BRUSH, DKGRAY_BRUSH, BLACK_BRUSH and NULL_BRUSH. */
    static const ULONG colors[] = {0xFFFFFF, 0xC0C0C0, 0x808080, 0x404040, 0, 0};
    if (index < WHITE_BRUSH || index > NULL_BRUSH)
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
        brush->style = index == NULL_BRUSH ? BS_NULL : BS_SOLID;
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
    LONG result = !output                           ? WIN32_LOGBRUSH_SIZE
                  : capacity >= WIN32_LOGBRUSH_SIZE ? WIN32_LOGBRUSH_SIZE
                                                    : 0;
    if (output && result)
    {
        lx_guest_store_u32((uint8 *)output + WIN32_LOGBRUSH_STYLE, brush->style);
        lx_guest_store_u32((uint8 *)output + WIN32_LOGBRUSH_COLOR, brush->color);
        lx_guest_store_u32((uint8 *)output + WIN32_LOGBRUSH_HATCH, brush->bitmap);
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
    brush->style = BS_PATTERN;
    brush->bitmap = handle;
    brush->width = bitmap->width;
    brush->height = bitmap->height;
    brush->depth = bitmap->depth;
    memcpy(brush->pixels, bitmap->pixels, bytes);
    /* A monochrome tile keeps only which pels are the bitmap's second colour (set bits). */
    if (brush->depth == 1 && bitmap->palette_count >= MONOCHROME_COLORS)
        for (size_t i = 0; i < bytes / sizeof *brush->pixels; i++)
            brush->pixels[i] = brush->pixels[i] == bitmap->palette[1] ? RGB_WHITE : 0;
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
    if (!brush || brush->style == BS_NULL || !rgb)
        return FALSE;
    if (brush->style == BS_HATCHED)
    {
        LONG px = (LONG)(((int64_t)x - dc->brush_origin_x) & HATCH_CELL_MASK),
             py = (LONG)(((int64_t)y - dc->brush_origin_y) & HATCH_CELL_MASK);
        BOOL ink = brush->bitmap == HS_HORIZONTAL  ? py == 0
                   : brush->bitmap == HS_VERTICAL  ? px == 0
                   : brush->bitmap == HS_FDIAGONAL ? ((px + py) & HATCH_CELL_MASK) == 0
                   : brush->bitmap == HS_BDIAGONAL ? ((px - py) & HATCH_CELL_MASK) == 0
                   : brush->bitmap == HS_CROSS
                       ? (px == 0 || py == 0)
                       : (((px + py) & HATCH_CELL_MASK) == 0 || ((px - py) & HATCH_CELL_MASK) == 0);
        if (!ink && dc->background_mode == TRANSPARENT)
            return FALSE;
        ULONG color = ink ? brush->color : dc->background_color;
        *rgb = (color & 255) << 16 | (color & 0xff00) | (color >> 16 & 255);
        return TRUE;
    }
    if (brush->style == BS_PATTERN)
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
        lx_guest_store_u32((uint8 *)previous + WIN32_POINT_X, (ULONG)dc->brush_origin_x);
        lx_guest_store_u32((uint8 *)previous + WIN32_POINT_Y, (ULONG)dc->brush_origin_y);
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
    lx_guest_store_u32((uint8 *)output + WIN32_POINT_X, (ULONG)dc->brush_origin_x);
    lx_guest_store_u32((uint8 *)output + WIN32_POINT_Y, (ULONG)dc->brush_origin_y);
    return TRUE;
}

ULONG CreateHatchBrush(LONG hatch, ULONG color)
{
    if (hatch < HS_HORIZONTAL || hatch > HS_DIAGCROSS)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ULONG handle = CreateSolidBrush(color);
    LxHandleBorrow borrow = {0};
    if (handle && lx_handle_borrow(handle, LX_HANDLE_OPEN32_BRUSH, &borrow))
    {
        Open32Brush *brush = borrow.resource;
        brush->style = BS_HATCHED;
        brush->bitmap = (ULONG)hatch;
        lx_handle_release(&borrow);
    }
    return handle;
}

/* DIB pattern brushes decode into the same retained RGB tile as bitmap patterns. */
ULONG open32DibPatternBrush(PVOID information, PVOID bits, ULONG usage, BOOL monochrome)
{
    LONG width = (LONG)lx_guest_load_u32((uint8 *)information + WIN32_BITMAPINFOHEADER_WIDTH),
         height = (LONG)lx_guest_load_u32((uint8 *)information + WIN32_BITMAPINFOHEADER_HEIGHT);
    LONG rows = height < 0 ? -height : height;
    ULONG dc = CreateCompatibleDC(0), result = 0;
    ULONG bitmap = dc ? CreateBitmap(width, rows, 1, TRUE_COLOR_BITS, NULL) : 0;
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
    if (brush->style == BS_PATTERN)
    {
        BOOL mono = brush->depth == 1;
        /* A monochrome DIB carries a black and a white colour-table entry. */
        size_t head = mono ? WIN32_BITMAPINFOHEADER_SIZE + MONOCHROME_COLORS * WIN32_RGBQUAD_SIZE
                           : WIN32_BITMAPINFOHEADER_SIZE;
        size_t stride = mono ? ((size_t)brush->width + DIB_ROW_ALIGN_BITS - 1) /
                                   DIB_ROW_ALIGN_BITS * DIB_ROW_ALIGN_BYTES
                             : (size_t)brush->width * PATTERN_TILE_BYTES_PER_PEL;
        dib = calloc(head + stride * brush->height, 1);
        if (dib)
        {
            lx_guest_store_u32(dib + WIN32_BITMAPINFOHEADER_STRUCT_SIZE,
                               WIN32_BITMAPINFOHEADER_SIZE);
            lx_guest_store_u32(dib + WIN32_BITMAPINFOHEADER_WIDTH, brush->width);
            lx_guest_store_u32(dib + WIN32_BITMAPINFOHEADER_HEIGHT, brush->height);
            lx_guest_store_u16(dib + WIN32_BITMAPINFOHEADER_PLANES, 1);
            lx_guest_store_u16(dib + WIN32_BITMAPINFOHEADER_BIT_COUNT,
                               mono ? 1 : PATTERN_TILE_BITS);
            if (mono)
            {
                lx_guest_store_u32(dib + WIN32_BITMAPINFOHEADER_CLR_USED, MONOCHROME_COLORS);
                lx_guest_store_u32(dib + WIN32_BITMAPINFOHEADER_SIZE + WIN32_RGBQUAD_SIZE,
                                   RGB_WHITE);
            }
            for (unsigned y = 0; y < brush->height; y++)
                for (unsigned x = 0; x < brush->width; x++)
                {
                    uint32 pixel = brush->pixels[(size_t)y * brush->width + x];
                    uint8 *row = dib + head + y * stride;
                    if (!mono)
                        lx_guest_store_u32(row + x * PATTERN_TILE_BYTES_PER_PEL, pixel);
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
    uint8 header[WIN32_BITMAPINFOHEADER_SIZE];
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
    ULONG style = lx_guest_load_u32((uint8 *)input + WIN32_LOGBRUSH_STYLE),
          color = lx_guest_load_u32((uint8 *)input + WIN32_LOGBRUSH_COLOR),
          hatch = lx_guest_load_u32((uint8 *)input + WIN32_LOGBRUSH_HATCH);
    if (style == BS_SOLID)
        return CreateSolidBrush(color);
    if (style == BS_HATCHED)
        return CreateHatchBrush((LONG)hatch, color);
    if (style == BS_PATTERN)
        return CreatePatternBrush(hatch);
    if (style == BS_DIBPATTERNPT)
        return packed_pattern_brush(hatch, color & DIB_USAGE_MASK);
    if (style == BS_DIBPATTERN)
    {
        ULONG result = packed_pattern_brush(GlobalLock(hatch), color & DIB_USAGE_MASK);
        GlobalUnlock(hatch);
        return result;
    }
    if (style == BS_NULL)
    {
        ULONG handle = CreateSolidBrush(0);
        LxHandleBorrow borrow = {0};
        if (handle && lx_handle_borrow(handle, LX_HANDLE_OPEN32_BRUSH, &borrow))
        {
            ((Open32Brush *)borrow.resource)->style = BS_NULL;
            lx_handle_release(&borrow);
        }
        return handle;
    }
    open32SetError(ERROR_INVALID_PARAMETER);
    return 0;
}
