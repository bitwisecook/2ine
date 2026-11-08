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
    TRACE_NATIVE("CreateSolidBrush(%08X)", (uint)color);
    /* PALETTERGB is ordinary RGB on our true-color display. Retain the
       specifier in LOGBRUSH; raster conversion already uses its RGB bytes. */
    if ((color & 0xFF000000u) && (color & 0xFF000000u) != 0x02000000u)
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

/* A rectangle under a rotating transform is a filled quadrilateral without an outline. */
BOOL open32RotatedFill(ULONG handle, LONG left, LONG top, LONG right, LONG bottom, ULONG brush)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc)
        return FALSE;
    uint8 points[32];
    const LONG corners[8] = {left, top, right, top, right, bottom, left, bottom};
    for (unsigned i = 0; i < 8; i++)
        lx_guest_store_u32(points + i * 4, (ULONG)corners[i]);
    ULONG old_brush = open32SelectBrush(dc, brush),
          old_pen = open32SelectPen(dc, GetStockObject(8));
    BOOL ok = old_brush && old_pen && Polygon(handle, points, 4);
    if (old_pen)
        open32SelectPen(dc, old_pen);
    if (old_brush)
        open32SelectBrush(dc, old_brush);
    return ok;
}

LONG FillRect(ULONG handle, PVOID rectangle, ULONG brush_handle)
{
    if (!open32CanDraw(handle) || !rectangle)
    {
        open32SetError(87);
        return 0;
    }
    if (open32Rotated(open32FindDC(handle)) && brush_handle > 21)
    {
        const uint8 *corner = rectangle;
        return open32RotatedFill(handle, (LONG)lx_guest_load_u32(corner),
                                 (LONG)lx_guest_load_u32(corner + 4),
                                 (LONG)lx_guest_load_u32(corner + 8),
                                 (LONG)lx_guest_load_u32(corner + 12), brush_handle);
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
    if (open32IsMetafile(handle))
    {
        if ((int64_t)right - left > INT32_MAX || (int64_t)right - left < INT32_MIN ||
            (int64_t)bottom - top > INT32_MAX || (int64_t)bottom - top < INT32_MIN)
        {
            lx_handle_release(&borrow);
            open32SetError(87);
            return 0;
        }
        ULONG recorded_brush = brush_handle, temporary = 0;
        if (brush_handle > 0 && brush_handle <= 21)
        {
            temporary = CreateSolidBrush(color);
            recorded_brush = temporary;
        }
        open32MetafileObject(handle, recorded_brush);
        open32MetafilePatBlt(handle, left, top, right - left, bottom - top, 0xf00021);
        Open32DC *recorded_dc = open32FindDC(handle);
        open32MetafileObject(handle, recorded_dc->brush_handle);
        if (temporary)
            DeleteObject(temporary);
    }
    Open32DC *mapping = open32FindDC(handle);
    if (!open32MapPoint(mapping, &left, &top, TRUE) ||
        !open32MapPoint(mapping, &right, &bottom, TRUE))
    {
        lx_handle_release(&borrow);
        return 0;
    }
    if (left > right)
    {
        LONG v = left;
        left = right;
        right = v;
    }
    if (top > bottom)
    {
        LONG v = top;
        top = bottom;
        bottom = v;
    }
    int64_t lower = (int64_t)open32DCHeight(handle) - bottom,
            upper = (int64_t)open32DCHeight(handle) - top;
    BOOL result = FALSE;
    if (lower >= INT32_MIN && upper <= INT32_MAX && lower <= upper && left <= right)
    {
        RECTL rect = {left, (LONG)lower, right, (LONG)upper};
        LONG rgb = (LONG)((color & 255) << 16 | (color & 0xFF00) | (color >> 16 & 255));
        if (style == 2 || style == 3)
        {
            if ((int64_t)right - left <= INT32_MAX && (int64_t)bottom - top <= INT32_MAX)
            {
                LONG lx = left, ly = top, rx = right, ry = bottom;
                if (open32MapPoint(mapping, &lx, &ly, FALSE) &&
                    open32MapPoint(mapping, &rx, &ry, FALSE))
                    result = brush_blt(handle, lx, ly, rx - lx, ry - ly, 0xf00021, borrow.resource);
            }
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
    open32MetafilePatBlt(handle, left, (LONG)lx_guest_load_u32((uint8 *)input + 4), right - left,
                         (LONG)lx_guest_load_u32((uint8 *)input + 12) -
                             (LONG)lx_guest_load_u32((uint8 *)input + 4),
                         0x550009);
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
    int64_t ex = (int64_t)x + width, ey = (int64_t)y + height;
    if (ex < INT32_MIN || ex > INT32_MAX || ey < INT32_MIN || ey > INT32_MAX)
    {
        open32SetError(87);
        return FALSE;
    }
    LONG mapped_x = (LONG)ex, mapped_y = (LONG)ey;
    if (!open32MapPoint(dc, &x, &y, TRUE) || !open32MapPoint(dc, &mapped_x, &mapped_y, TRUE))
        return FALSE;
    ex = (int64_t)mapped_x - x;
    ey = (int64_t)mapped_y - y;
    if (ex < INT32_MIN || ex > INT32_MAX || ey < INT32_MIN || ey > INT32_MAX)
    {
        open32SetError(87);
        return FALSE;
    }
    width = (LONG)ex;
    height = (LONG)ey;
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
            if (pattern && brush->style == 2)
            {
                LONG xx = (LONG)(left + i % w), yy = (LONG)(bottom - 1 - i / w);
                LONG px = (xx - dc->brush_origin_x) & 7, py = (yy - dc->brush_origin_y) & 7;
                BOOL ink = brush->bitmap == 0   ? py == 0
                           : brush->bitmap == 1 ? px == 0
                           : brush->bitmap == 2 ? ((px + py) & 7) == 0
                           : brush->bitmap == 3 ? ((px - py) & 7) == 0
                           : brush->bitmap == 4 ? (px == 0 || py == 0)
                                                : (((px + py) & 7) == 0 || ((px - py) & 7) == 0);
                if (!ink && dc->background_mode == 1)
                    continue;
                ULONG color = ink ? brush->color : dc->background_color;
                p = (color & 255) << 16 | (color & 0xff00) | (color >> 16 & 255);
            }
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
    open32MetafilePatBlt(handle, x, y, width, height, operation);
    if (dc && open32Rotated(dc) && operation == 0xF00021)
        return open32RotatedFill(handle, x, y, x + width, y + height, dc->brush_handle);
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

ULONG CreateHatchBrush(LONG hatch, ULONG color)
{
    if (hatch < 0 || hatch > 5)
    {
        open32SetError(87);
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
    if (!address || !lx_execution_read(GLoaderState.execution, address, header, sizeof header) ||
        !open32DibBuffers(header, 0, UINT32_MAX, usage, &info_bytes, &pixel_bytes) ||
        !pixel_bytes ||
        !lx_execution_borrow(GLoaderState.execution, address, info_bytes + pixel_bytes,
                             LX_GUEST_READ, &dib))
    {
        open32SetError(87);
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
        open32SetError(packed ? 87 : 8);
    free(packed);
    return result;
}

ULONG CreateBrushIndirect(PVOID input)
{
    if (!input)
    {
        open32SetError(87);
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
    open32SetError(87);
    return 0;
}
