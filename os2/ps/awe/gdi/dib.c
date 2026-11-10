/* Public domain. No rights reserved. Wholly AI-authored. */
/* DIB rows use DWORD padding; positive heights retain bottom-up bitmap order. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/private/pmgrep.h"
#include <math.h>

enum
{
    /* The largest DIB side, and the most pels one transfer draws. */
    DIB_SIDE_MAX = 16384,
    BLIT_AREA_MAX = 16 * 1024 * 1024,
    /* BLACKONWHITE/WHITEONBLACK shrinking gives up after this many source reads. */
    STRETCH_SAMPLES_MAX = 64 * 1024 * 1024,
    /* DIB rows are padded to whole 32-bit words. */
    DIB_ROW_ALIGN_BITS = 32,
    DIB_ROW_ALIGN_BYTES = 4,
    /* A DIB_PAL_COLORS colour table holds 16-bit palette indices. */
    PAL_COLORS_ENTRY_SIZE = 2,
    /* BI_BITFIELDS puts red, green and blue DWORD masks after the header. */
    BITFIELDS_RED = 0,
    BITFIELDS_GREEN = 4,
    BITFIELDS_BLUE = 8,
    BITFIELDS_SIZE = 12,
    /* A colour-table query (no pels) asks for formats below 16 bits per pel. */
    COLOR_TABLE_DEPTH_LIMIT = 16,
    INDEXED_DEPTH_MAX = 8,
    INDEXED_COLORS_MAX = 256,
    TRUE_COLOR_BITS = 24,
    RGB_MASK = 0xFFFFFF,
    /* A ternary raster operation keeps its operation index in bits 16-23. */
    RASTER_OPERATION_INDEX_SHIFT = 16,
    RASTER_OPERATION_INDEX_MASK = 0xff,
    /* BI_RLE8/BI_RLE4 escapes: a zero count byte followed by one of these. */
    RLE_END_OF_LINE = 0,
    RLE_END_OF_BITMAP = 1,
    RLE_DELTA = 2
};

BOOL open32DibBuffers(PVOID information, ULONG start, ULONG count, ULONG usage,
                      size_t *information_bytes, size_t *pixel_bytes)
{
    if (!information ||
        lx_guest_load_u32((uint8 *)information + WIN32_BITMAPINFOHEADER_STRUCT_SIZE) !=
            WIN32_BITMAPINFOHEADER_SIZE ||
        usage > WIN32_DIB_PAL_COLORS)
    {
        open32SetError(usage > WIN32_DIB_PAL_COLORS ? ERROR_NOT_SUPPORTED
                                                    : ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const uint8 *info = information;
    unsigned depth = lx_guest_load_u16(info + WIN32_BITMAPINFOHEADER_BIT_COUNT);
    *information_bytes = WIN32_BITMAPINFOHEADER_SIZE;
    *pixel_bytes = 0;
    if (!depth)
        return TRUE;
    LONG width = (LONG)lx_guest_load_u32(info + WIN32_BITMAPINFOHEADER_WIDTH),
         height = (LONG)lx_guest_load_u32(info + WIN32_BITMAPINFOHEADER_HEIGHT);
    uint64_t rows = height < 0 ? -(int64_t)height : height;
    ULONG compression = lx_guest_load_u32(info + WIN32_BITMAPINFOHEADER_COMPRESSION);
    BOOL compressed = compression == WIN32_BI_RLE8 || compression == WIN32_BI_RLE4;
    if (width < 1 || width > DIB_SIDE_MAX || !rows || rows > DIB_SIDE_MAX || start > rows ||
        lx_guest_load_u16(info + WIN32_BITMAPINFOHEADER_PLANES) != 1 ||
        (depth != 1 && depth != 4 && depth != 8 && depth != 16 && depth != 24 && depth != 32) ||
        compression > WIN32_BI_BITFIELDS || (compression == WIN32_BI_RLE8 && depth != 8) ||
        (compression == WIN32_BI_RLE4 && depth != 4) ||
        (compression == WIN32_BI_BITFIELDS && depth != 16 && depth != 32) ||
        (compressed && height < 0))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    unsigned colors = lx_guest_load_u32(info + WIN32_BITMAPINFOHEADER_CLR_USED);
    if (depth <= INDEXED_DEPTH_MAX)
    {
        if (colors > (1u << depth))
        {
            open32SetError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        /* DIB_RGB_COLORS tables hold RGBQUADs; DIB_PAL_COLORS tables hold palette indices. */
        *information_bytes +=
            (colors ? colors : 1u << depth) * (usage ? PAL_COLORS_ENTRY_SIZE : WIN32_RGBQUAD_SIZE);
    }
    else
        /* Bitfield masks precede any optional palette, which true-color pels do not use. */
        *information_bytes += (compression == WIN32_BI_BITFIELDS ? BITFIELDS_SIZE : 0) +
                              (size_t)colors * WIN32_RGBQUAD_SIZE;
    uint64_t stride = (((uint64_t)width * depth + DIB_ROW_ALIGN_BITS - 1) / DIB_ROW_ALIGN_BITS) *
                      DIB_ROW_ALIGN_BYTES;
    if (count > rows - start)
        count = (ULONG)(rows - start);
    *pixel_bytes = compressed ? lx_guest_load_u32(info + WIN32_BITMAPINFOHEADER_SIZE_IMAGE)
                              : (size_t)(stride * count);
    return TRUE;
}

/* Expands BI_RLE8 or BI_RLE4 into DWORD-aligned bottom-up index rows. */
static BOOL rle_expand(const uint8 *input, size_t size, unsigned width, unsigned rows,
                       unsigned depth, uint8 *output, size_t stride)
{
    size_t at = 0;
    unsigned x = 0, y = 0;
#define RLE_PUT(value)                                                                             \
    do                                                                                             \
    {                                                                                              \
        if (x < width && y < rows)                                                                 \
        {                                                                                          \
            uint8 *row = output + (size_t)y * stride;                                              \
            if (depth == 8)                                                                        \
                row[x] = (uint8)(value);                                                           \
            else                                                                                   \
                row[x / 2] |= (uint8)(((value) & 15) << (x & 1 ? 0 : 4));                          \
        }                                                                                          \
        x++;                                                                                       \
    } while (0)
    while (at + 1 < size)
    {
        unsigned first = input[at++], second = input[at++];
        if (first)
        {
            for (unsigned i = 0; i < first; i++)
                RLE_PUT(depth == 8 ? second : i & 1 ? second & 15 : second >> 4);
            continue;
        }
        if (second == RLE_END_OF_LINE)
        {
            x = 0;
            y++;
        }
        else if (second == RLE_END_OF_BITMAP)
            return TRUE;
        else if (second == RLE_DELTA)
        {
            if (at + 2 > size)
                return FALSE;
            x += input[at++];
            y += input[at++];
        }
        else
        {
            /* An absolute run of literal pels, padded to a 16-bit boundary. */
            size_t bytes = depth == 8 ? second : (second + 1) / 2;
            if (bytes > size - at)
                return FALSE;
            for (unsigned i = 0; i < second; i++)
                RLE_PUT(depth == 8 ? input[at + i]
                        : i & 1    ? input[at + i / 2] & 15
                                   : input[at + i / 2] >> 4);
            at += (bytes + 1) & ~(size_t)1;
        }
    }
#undef RLE_PUT
    return TRUE;
}

static unsigned mask_channel(ULONG value, ULONG mask)
{
    if (!mask)
        return 0;
    unsigned shift = 0, bits = 0;
    while (!((mask >> shift) & 1))
        shift++;
    while (shift + bits < 32 && ((mask >> (shift + bits)) & 1))
        bits++;
    ULONG channel = (value & mask) >> shift;
    return bits >= 8 ? (unsigned)(channel >> (bits - 8))
                     : (unsigned)(channel * 255 / ((1u << bits) - 1));
}

static uint32 palette_color(ULONG dc, ULONG index)
{
    Open32DC *context = open32FindDC(dc);
    uint8 entry[WIN32_PALETTEENTRY_SIZE] = {0};
    if (context)
        GetPaletteEntries(context->palette_handle, index, 1, entry);
    return (uint32)entry[WIN32_PALETTEENTRY_RED] << 16 |
           (uint32)entry[WIN32_PALETTEENTRY_GREEN] << 8 | entry[WIN32_PALETTEENTRY_BLUE];
}

static unsigned nearest(const uint32 *colors, unsigned count, uint32 rgb)
{
    unsigned best = 0, distance = UINT32_MAX;
    for (unsigned i = 0; i < count; i++)
    {
        int r = (int)((rgb >> 16) & 255) - (int)((colors[i] >> 16) & 255);
        int g = (int)((rgb >> 8) & 255) - (int)((colors[i] >> 8) & 255);
        int b = (int)(rgb & 255) - (int)(colors[i] & 255);
        unsigned next = (unsigned)(r * r + g * g + b * b);
        if (next < distance)
        {
            best = i;
            distance = next;
            if (!next)
                break;
        }
    }
    return best;
}

LONG GetDIBits(ULONG dc, ULONG handle, ULONG start, ULONG count, PVOID bits, PVOID information,
               ULONG usage)
{
    TRACE_NATIVE("GetDIBits(%u, %u, %u, %u, %p, %p, %u)", dc, handle, start, count, bits,
                 information, usage);
    size_t info_bytes, pixel_bytes;
    if (!open32FindDC(dc))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!bits && information)
    {
        /* Without pixels the caller is asking about the bitmap, so its header need not
           be complete. A depth of 1-15 asks for the colour table of that format. */
        uint8 *query = information;
        unsigned requested = lx_guest_load_u16(query + WIN32_BITMAPINFOHEADER_BIT_COUNT);
        LxHandleBorrow source = {0};
        PmBitmap *described = lx_pm_bitmap(handle, &source);
        if (!described)
        {
            lx_handle_release(&source);
            open32SetError(ERROR_INVALID_HANDLE);
            return 0;
        }
        if (lx_guest_load_u32(query + WIN32_BITMAPINFOHEADER_STRUCT_SIZE) !=
                WIN32_BITMAPINFOHEADER_SIZE &&
            (!requested || requested >= COLOR_TABLE_DEPTH_LIMIT))
        {
            lx_handle_release(&source);
            open32SetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        unsigned depth = requested ? requested : described->depth;
        lx_guest_store_u32(query + WIN32_BITMAPINFOHEADER_WIDTH, described->width);
        lx_guest_store_u32(query + WIN32_BITMAPINFOHEADER_HEIGHT, described->height);
        lx_guest_store_u16(query + WIN32_BITMAPINFOHEADER_PLANES, 1);
        lx_guest_store_u16(query + WIN32_BITMAPINFOHEADER_BIT_COUNT, depth);
        if (!lx_guest_load_u32(query + WIN32_BITMAPINFOHEADER_SIZE_IMAGE))
            lx_guest_store_u32(
                query + WIN32_BITMAPINFOHEADER_SIZE_IMAGE,
                ((described->width * depth + DIB_ROW_ALIGN_BITS - 1) / DIB_ROW_ALIGN_BITS) *
                    DIB_ROW_ALIGN_BYTES * described->height);
        lx_handle_release(&source);
        if (!requested || requested >= COLOR_TABLE_DEPTH_LIMIT)
            return 1;
        count = 0;
    }
    if (!open32DibBuffers(information, start, count, usage, &info_bytes, &pixel_bytes))
        return 0;
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    if (!bitmap)
    {
        lx_handle_release(&borrow);
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    uint8 *info = information;
    unsigned depth = lx_guest_load_u16(info + WIN32_BITMAPINFOHEADER_BIT_COUNT);
    LONG height = (LONG)lx_guest_load_u32(info + WIN32_BITMAPINFOHEADER_HEIGHT);
    if (lx_guest_load_u32(info + WIN32_BITMAPINFOHEADER_WIDTH) != bitmap->width ||
        (uint64_t)(height < 0 ? -(int64_t)height : height) != bitmap->height)
    {
        lx_handle_release(&borrow);
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    uint32 colors[INDEXED_COLORS_MAX] = {0};
    if (lx_guest_load_u32(info + WIN32_BITMAPINFOHEADER_COMPRESSION) == WIN32_BI_RLE8 ||
        lx_guest_load_u32(info + WIN32_BITMAPINFOHEADER_COMPRESSION) == WIN32_BI_RLE4)
    {
        lx_handle_release(&borrow);
        open32SetError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    unsigned color_count = depth <= INDEXED_DEPTH_MAX
                               ? (unsigned)((info_bytes - WIN32_BITMAPINFOHEADER_SIZE) /
                                            (usage ? PAL_COLORS_ENTRY_SIZE : WIN32_RGBQUAD_SIZE))
                               : 0;
    open32BitmapPalette(depth, colors);
    for (unsigned i = 0; i < color_count; i++)
    {
        if (usage)
        {
            colors[i] = palette_color(dc, i);
            lx_guest_store_u16(info + WIN32_BITMAPINFOHEADER_SIZE + i * PAL_COLORS_ENTRY_SIZE,
                               (uint16)i);
            continue;
        }
        if (depth == bitmap->depth && bitmap->palette_count)
            colors[i] = i < bitmap->palette_count ? bitmap->palette[i] : 0;
        lx_guest_store_u32(info + WIN32_BITMAPINFOHEADER_SIZE + i * WIN32_RGBQUAD_SIZE, colors[i]);
    }
    unsigned stride = ((bitmap->width * depth + DIB_ROW_ALIGN_BITS - 1) / DIB_ROW_ALIGN_BITS) *
                      DIB_ROW_ALIGN_BYTES;
    lx_guest_store_u32(info + WIN32_BITMAPINFOHEADER_SIZE_IMAGE, stride * bitmap->height);
    lx_guest_store_u32(info + WIN32_BITMAPINFOHEADER_CLR_USED, color_count);
    if (count > bitmap->height - start)
        count = bitmap->height - start;
    if (bits && pixel_bytes)
    {
        memset(bits, 0, pixel_bytes);
        for (unsigned row = 0; row < count; row++)
        {
            unsigned source = height < 0 ? bitmap->height - 1 - start - row : start + row;
            uint8 *output = (uint8 *)bits + (size_t)row * stride;
            for (unsigned x = 0; x < bitmap->width; x++)
            {
                uint32 rgb = bitmap->pixels[(size_t)source * bitmap->width + x];
                if (depth <= INDEXED_DEPTH_MAX)
                {
                    unsigned index = nearest(colors, color_count, rgb), bit = x * depth;
                    output[bit / 8] |= (uint8)(index << (8 - depth - bit % 8));
                }
                else if (depth == 16)
                    lx_guest_store_u16(
                        output + x * 2,
                        (uint16)((rgb >> 9 & 0x7c00) | (rgb >> 6 & 0x3e0) | (rgb >> 3 & 0x1f)));
                else
                {
                    uint8 *pixel = output + x * (depth / 8);
                    pixel[0] = (uint8)rgb;
                    pixel[1] = (uint8)(rgb >> 8);
                    pixel[2] = (uint8)(rgb >> 16);
                }
            }
        }
    }
    LONG result = bits ? (LONG)count : (LONG)bitmap->height;
    lx_handle_release(&borrow);
    return result;
}

LONG SetDIBits(ULONG dc, ULONG handle, ULONG start, ULONG count, PVOID bits, PVOID information,
               ULONG usage)
{
    TRACE_NATIVE("SetDIBits(%u, %u, %u, %u, %p, %p, %u)", dc, handle, start, count, bits,
                 information, usage);
    size_t info_bytes, pixel_bytes;
    if (!open32FindDC(dc))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!open32DibBuffers(information, start, count, usage, &info_bytes, &pixel_bytes))
        return 0;
    const uint8 *info = information;
    unsigned depth = lx_guest_load_u16(info + WIN32_BITMAPINFOHEADER_BIT_COUNT);
    if (!depth || (pixel_bytes && !bits))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    if (!bitmap)
    {
        lx_handle_release(&borrow);
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    LONG height = (LONG)lx_guest_load_u32(info + WIN32_BITMAPINFOHEADER_HEIGHT);
    if (lx_guest_load_u32(info + WIN32_BITMAPINFOHEADER_WIDTH) != bitmap->width ||
        (uint64_t)(height < 0 ? -(int64_t)height : height) != bitmap->height)
    {
        lx_handle_release(&borrow);
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (count > bitmap->height - start)
        count = bitmap->height - start;
    if (!count)
    {
        lx_handle_release(&borrow);
        return 0;
    }
    uint32 palette[INDEXED_COLORS_MAX] = {0}, destination_colors[INDEXED_COLORS_MAX];
    unsigned colors = depth <= INDEXED_DEPTH_MAX
                          ? (unsigned)((info_bytes - WIN32_BITMAPINFOHEADER_SIZE) /
                                       (usage ? PAL_COLORS_ENTRY_SIZE : WIN32_RGBQUAD_SIZE))
                          : 0;
    ULONG compression = lx_guest_load_u32(info + WIN32_BITMAPINFOHEADER_COMPRESSION);
    /* Without BI_BITFIELDS, 16-bit pels are 5-5-5 RGB and 32-bit pels 8-8-8. */
    ULONG red_mask = depth == 32 ? 0xFF0000 : 0x7C00, green_mask = depth == 32 ? 0xFF00 : 0x3E0,
          blue_mask = depth == 32 ? 0xFF : 0x1F;
    if (compression == WIN32_BI_BITFIELDS)
    {
        const uint8 *masks = info + WIN32_BITMAPINFOHEADER_SIZE;
        red_mask = lx_guest_load_u32(masks + BITFIELDS_RED);
        green_mask = lx_guest_load_u32(masks + BITFIELDS_GREEN);
        blue_mask = lx_guest_load_u32(masks + BITFIELDS_BLUE);
    }
    for (unsigned i = 0; i < colors; i++)
        palette[i] =
            usage ? palette_color(dc, lx_guest_load_u16(info + WIN32_BITMAPINFOHEADER_SIZE +
                                                        i * PAL_COLORS_ENTRY_SIZE))
                  : lx_guest_load_u32(info + WIN32_BITMAPINFOHEADER_SIZE + i * WIN32_RGBQUAD_SIZE) &
                        RGB_MASK;
    open32BitmapPalette(bitmap->depth, destination_colors);
    if (bitmap->depth == depth && colors)
        memcpy(destination_colors, palette, sizeof palette);
    else if (bitmap->palette_count)
        memcpy(destination_colors, bitmap->palette, sizeof bitmap->palette);
    uint32 *decoded = malloc((size_t)bitmap->width * count * sizeof *decoded);
    if (!decoded)
    {
        lx_handle_release(&borrow);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    BOOL valid = TRUE;
    unsigned stride = ((bitmap->width * depth + DIB_ROW_ALIGN_BITS - 1) / DIB_ROW_ALIGN_BITS) *
                      DIB_ROW_ALIGN_BYTES;
    uint8 *expanded = NULL;
    if (compression == WIN32_BI_RLE8 || compression == WIN32_BI_RLE4)
    {
        expanded = calloc((size_t)stride * bitmap->height, 1);
        if (!expanded)
        {
            free(decoded);
            lx_handle_release(&borrow);
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        valid =
            rle_expand(bits, pixel_bytes, bitmap->width, bitmap->height, depth, expanded, stride);
    }
    for (unsigned row = 0; row < count && valid; row++)
    {
        const uint8 *source = expanded ? expanded + (size_t)(start + row) * stride
                                       : (const uint8 *)bits + (size_t)row * stride;
        for (unsigned x = 0; x < bitmap->width; x++)
        {
            uint32 rgb;
            if (depth <= INDEXED_DEPTH_MAX)
            {
                unsigned bit = x * depth,
                         index = (source[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
                if (index >= colors)
                {
                    valid = FALSE;
                    break;
                }
                rgb = palette[index];
            }
            else if (depth == 16 || compression == WIN32_BI_BITFIELDS)
            {
                ULONG value = depth == 16 ? lx_guest_load_u16(source + x * 2)
                                          : lx_guest_load_u32(source + x * 4);
                rgb = (uint32)mask_channel(value, red_mask) << 16 |
                      (uint32)mask_channel(value, green_mask) << 8 | mask_channel(value, blue_mask);
            }
            else
            {
                const uint8 *pixel = source + x * (depth / 8);
                rgb = (uint32)pixel[2] << 16 | (uint32)pixel[1] << 8 | pixel[0];
            }
            if (bitmap->depth <= INDEXED_DEPTH_MAX)
                rgb = destination_colors[nearest(destination_colors, 1u << bitmap->depth, rgb)];
            decoded[(size_t)row * bitmap->width + x] = rgb;
        }
    }
    if (valid)
    {
        if (bitmap->depth <= INDEXED_DEPTH_MAX)
        {
            bitmap->palette_count = 1u << bitmap->depth;
            memcpy(bitmap->palette, destination_colors, sizeof bitmap->palette);
        }
        for (unsigned row = 0; row < count; row++)
        {
            unsigned target = height < 0 ? bitmap->height - 1 - start - row : start + row;
            memcpy(bitmap->pixels + (size_t)target * bitmap->width,
                   decoded + (size_t)row * bitmap->width, (size_t)bitmap->width * sizeof *decoded);
        }
    }
    free(decoded);
    free(expanded);
    lx_handle_release(&borrow);
    if (!valid)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return (LONG)count;
}

/* BLACKONWHITE and WHITEONBLACK reduce all covered source pels by AND and OR. */
static BOOL stretch_sample(const PmBitmap *bitmap, LONG image_height, LONG sx, LONG sy, LONG sw,
                           LONG sh, double u0, double u1, double v0, double v1, LONG mode,
                           uint64 *work, uint32 *rgb)
{
    LONG first_x = (LONG)floor(u0), last_x = (LONG)ceil(u1), first_y = (LONG)floor(v0),
         last_y = (LONG)ceil(v1);
    if (first_x < 0)
        first_x = 0;
    if (first_y < 0)
        first_y = 0;
    LONG maximum_x = sw < 0 ? -sw : sw, maximum_y = sh < 0 ? -sh : sh;
    if (last_x > maximum_x)
        last_x = maximum_x;
    if (last_y > maximum_y)
        last_y = maximum_y;
    uint32 combined = mode == BLACKONWHITE ? RGB_WHITE : 0;
    for (LONG v = first_y; v < last_y; v++)
        for (LONG u = first_x; u < last_x; u++)
        {
            double w = (fmin(u1, u + 1.0) - fmax(u0, (double)u)) *
                       (fmin(v1, v + 1.0) - fmax(v0, (double)v));
            if (w <= 0)
                continue;
            if (++*work > STRETCH_SAMPLES_MAX)
            {
                open32SetError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
            LONG x = sw > 0 ? sx + u : sx - 1 - u,
                 y = image_height > 0 ? (sh > 0 ? sy + sh - 1 - v : sy + sh + v)
                                      : (LONG)bitmap->height - 1 - (sh > 0 ? sy + v : sy - 1 - v);
            if (x < 0 || y < 0 || (ULONG)x >= bitmap->width || (ULONG)y >= bitmap->height)
            {
                open32SetError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }
            uint32 pixel = bitmap->pixels[(size_t)y * bitmap->width + x];
            if (mode == BLACKONWHITE)
                combined &= pixel;
            else
                combined |= pixel;
        }
    *rgb = combined;
    return TRUE;
}

typedef struct RotatedBlit
{
    Open32DC *dc;
    const PmBitmap *bitmap;
    LONG canvas_height, ax, ay;
    double ex, ey, fx, fy, determinant;
    LONG image_height, source_x, source_y, source_width, source_height;
    unsigned rop;
    BOOL pattern;
} RotatedBlit;

static int rotated_pel(void *context, LONG px, LONG device_y, uint32_t *pel)
{
    RotatedBlit *b = context;
    LONG py = b->canvas_height - 1 - device_y;
    double rx = px + 0.5 - b->ax, ry = py + 0.5 - b->ay;
    double u = (rx * b->fy - ry * b->fx) / b->determinant,
           v = (ry * b->ex - rx * b->ey) / b->determinant;
    if (u < 0 || u >= 1 || v < 0 || v >= 1)
        return LX_PM_PEL_KEEP;
    LONG rows = (LONG)b->bitmap->height;
    LONG span_x = b->source_width < 0 ? -b->source_width : b->source_width,
         span_y = b->source_height < 0 ? -b->source_height : b->source_height;
    LONG su = (LONG)(u * span_x), sv = (LONG)(v * span_y);
    LONG sx = b->source_width > 0 ? b->source_x + su : b->source_x - 1 - su;
    LONG sy = b->image_height > 0
                  ? (b->source_height > 0 ? b->source_y + b->source_height - 1 - sv
                                          : b->source_y + b->source_height + sv)
                  : rows - 1 - (b->source_height > 0 ? b->source_y + sv : b->source_y - 1 - sv);
    if (sx < 0 || sy < 0 || (ULONG)sx >= b->bitmap->width || (ULONG)sy >= b->bitmap->height)
        return LX_PM_PEL_KEEP;
    uint32 p = 0, s = b->bitmap->pixels[(size_t)sy * b->bitmap->width + sx];
    if (b->pattern && !open32BrushPixel(b->dc, px, py, &p))
        return LX_PM_PEL_KEEP;
    *pel = lx_pm_rop3(p, s, *pel, b->rop);
    return LX_PM_PEL_SET;
}

typedef struct StretchBlit
{
    Open32DC *dc;
    const PmBitmap *bitmap;
    LONG canvas_height, x, y;
    int64_t dw, dh;
    LONG image_height, rows, source_x, source_y, source_width, source_height;
    unsigned rop;
    BOOL pattern;
    uint64 sampling_work;
} StretchBlit;

/* Maps one target pel back to its source pel, or under BLACKONWHITE/WHITEONBLACK to the
   combination of the source pels it covers, and applies the raster operation. */
static int stretch_pel(void *context, LONG px, LONG device_y, uint32_t *pel)
{
    StretchBlit *b = context;
    LONG py = b->canvas_height - 1 - device_y;
    int64_t dw = b->dw, dh = b->dh;
    int64_t u = (dw > 0 ? (int64_t)px - b->x : (int64_t)b->x - 1 - px) *
                llabs((int64_t)b->source_width) / llabs(dw);
    int64_t v = (dh > 0 ? (int64_t)py - b->y : (int64_t)b->y - 1 - py) *
                llabs((int64_t)b->source_height) / llabs(dh);
    LONG sx = b->source_width > 0 ? b->source_x + (LONG)u : b->source_x - 1 - (LONG)u;
    LONG sy = b->image_height > 0
                  ? (b->source_height > 0 ? b->source_y + b->source_height - 1 - (LONG)v
                                          : b->source_y + b->source_height + (LONG)v)
                  : b->rows - 1 -
                        (b->source_height > 0 ? b->source_y + (LONG)v : b->source_y - 1 - (LONG)v);
    uint32 p = 0, s = b->bitmap->pixels[(size_t)sy * b->bitmap->width + sx];
    if ((b->dc->stretch_mode == BLACKONWHITE || b->dc->stretch_mode == WHITEONBLACK) &&
        (llabs(dw) < llabs((int64_t)b->source_width) ||
         llabs(dh) < llabs((int64_t)b->source_height)))
    {
        double column = dw > 0 ? (double)((int64_t)px - b->x) : (double)((int64_t)b->x - 1 - px),
               row = dh > 0 ? (double)((int64_t)py - b->y) : (double)((int64_t)b->y - 1 - py);
        double xs = fabs((double)b->source_width / dw), ys = fabs((double)b->source_height / dh);
        if (!stretch_sample(b->bitmap, b->image_height, b->source_x, b->source_y, b->source_width,
                            b->source_height, column * xs, (column + 1) * xs, row * ys,
                            (row + 1) * ys, b->dc->stretch_mode, &b->sampling_work, &s))
            return LX_PM_PEL_FAIL;
    }
    if (b->pattern && !open32BrushPixel(b->dc, px, py, &p))
        return LX_PM_PEL_FAIL;
    *pel = lx_pm_rop3(p, s, *pel, b->rop);
    return LX_PM_PEL_SET;
}

/* Under a rotating transform the target is a parallelogram: every device pel inside it maps back
   through the transform to one source pel, which then goes through the raster operation. */
static BOOL rotated_blit(ULONG handle, Open32DC *dc, const PmBitmap *bitmap, LONG image_height,
                         LONG x, LONG y, LONG width, LONG height, LONG source_x, LONG source_y,
                         LONG source_width, LONG source_height, ULONG operation)
{
    LONG ax = x, ay = y, bx = x + width, by = y, cx = x, cy = y + height;
    LONG dx = x + width, dy = y + height;
    if (!open32MapPoint(dc, &ax, &ay, TRUE) || !open32MapPoint(dc, &bx, &by, TRUE) ||
        !open32MapPoint(dc, &cx, &cy, TRUE) || !open32MapPoint(dc, &dx, &dy, TRUE))
        return FALSE;
    double ex = (double)bx - ax, ey = (double)by - ay, fx = (double)cx - ax, fy = (double)cy - ay;
    double determinant = ex * fy - ey * fx;
    if (determinant == 0)
        return TRUE;
    LONG left = ax, right = ax, top = ay, bottom = ay;
    const LONG xs[3] = {bx, cx, dx}, ys[3] = {by, cy, dy};
    for (unsigned i = 0; i < 3; i++)
    {
        left = xs[i] < left ? xs[i] : left;
        right = xs[i] > right ? xs[i] : right;
        top = ys[i] < top ? ys[i] : top;
        bottom = ys[i] > bottom ? ys[i] : bottom;
    }
    LONG canvas_width = open32DCWidth(handle), canvas_height = open32DCHeight(handle);
    left = left < 0 ? 0 : left;
    top = top < 0 ? 0 : top;
    right = right > canvas_width ? canvas_width : right;
    bottom = bottom > canvas_height ? canvas_height : bottom;
    if (left >= right || top >= bottom)
        return TRUE;
    if ((size_t)(right - left) * (size_t)(bottom - top) > BLIT_AREA_MAX)
        return FALSE;
    unsigned rop = (operation >> RASTER_OPERATION_INDEX_SHIFT) & RASTER_OPERATION_INDEX_MASK;
    RotatedBlit blit = {dc,
                        bitmap,
                        canvas_height,
                        ax,
                        ay,
                        ex,
                        ey,
                        fx,
                        fy,
                        determinant,
                        image_height,
                        source_x,
                        source_y,
                        source_width,
                        source_height,
                        rop,
                        lx_pm_rop_uses_pattern(rop)};
    RECTL device = {left, canvas_height - bottom, right, canvas_height - top};
    return lx_pm_blit_rect(handle, &device, LX_PM_BLIT_READ, rotated_pel, &blit);
}

LONG StretchDIBits(ULONG handle, LONG x, LONG y, LONG width, LONG height, LONG source_x,
                   LONG source_y, LONG source_width, LONG source_height, PVOID bits,
                   PVOID information, ULONG usage, ULONG operation)
{
    TRACE_NATIVE("StretchDIBits(%u, %d, %d, %d, %d, %d, %d, %d, %d, %p, %p, %u, %X)", handle, x, y,
                 width, height, source_x, source_y, source_width, source_height, bits, information,
                 usage, operation);
    Open32DC *dc = open32FindDC(handle);
    size_t info_bytes, pixel_bytes;
    if (!dc || dc->foreign_window || dc->kind == OPEN32_DC_DISPLAY_INFORMATION ||
        dc->kind == OPEN32_DC_PRINTER_INFORMATION || !bits ||
        !open32DibBuffers(information, 0, UINT32_MAX, usage, &info_bytes, &pixel_bytes) ||
        !pixel_bytes)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!width || !height || !source_width || !source_height)
        return 0;
    open32MetafileDib(handle, x, y, width, height, source_x, source_y, source_width, source_height,
                      bits, information, usage, operation);
    LONG image_width = (LONG)lx_guest_load_u32((uint8 *)information + WIN32_BITMAPINFOHEADER_WIDTH);
    LONG image_height =
        (LONG)lx_guest_load_u32((uint8 *)information + WIN32_BITMAPINFOHEADER_HEIGHT);
    LONG rows = image_height < 0 ? -image_height : image_height;
    int64_t sx_end = (int64_t)source_x + source_width;
    int64_t sy_end = (int64_t)source_y + source_height;
    int64_t x_end = (int64_t)x + width, y_end = (int64_t)y + height;
    if (source_x < 0 || sx_end < 0 || source_x > image_width || sx_end > image_width ||
        source_y < 0 || sy_end < 0 || source_y > rows || sy_end > rows || x_end < INT32_MIN ||
        x_end > INT32_MAX || y_end < INT32_MIN || y_end > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (open32Rotated(dc))
    {
        ULONG temporary = CreateBitmap(image_width, rows, 1, TRUE_COLOR_BITS, NULL);
        LxHandleBorrow lease = {0};
        PmBitmap *decoded = temporary ? lx_pm_bitmap(temporary, &lease) : NULL;
        BOOL drawn =
            decoded &&
            SetDIBits(handle, temporary, 0, (ULONG)rows, bits, information, usage) == rows &&
            rotated_blit(handle, dc, decoded, image_height, x, y, width, height, source_x, source_y,
                         source_width, source_height, operation);
        lx_handle_release(&lease);
        if (temporary)
            DeleteObject(temporary);
        if (!drawn)
        {
            open32SetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        return source_height < 0 ? -source_height : source_height;
    }
    LONG x2 = (LONG)x_end, y2 = (LONG)y_end;
    if (!open32MapPoint(dc, &x, &y, TRUE) || !open32MapPoint(dc, &x2, &y2, TRUE))
        return 0;
    int64_t dw = (int64_t)x2 - x, dh = (int64_t)y2 - y;
    if (!dw || !dh)
        return 0;
    LONG left = x < x2 ? x : x2, right = x < x2 ? x2 : x;
    LONG top = y < y2 ? y : y2, bottom = y < y2 ? y2 : y;
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
        return source_height < 0 ? -source_height : source_height;
    size_t area = (size_t)(right - left) * (size_t)(bottom - top);
    if (area > BLIT_AREA_MAX)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    ULONG bitmap_handle = CreateBitmap(image_width, rows, 1, TRUE_COLOR_BITS, NULL);
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = bitmap_handle ? lx_pm_bitmap(bitmap_handle, &borrow) : NULL;
    BOOL ok = bitmap &&
              SetDIBits(handle, bitmap_handle, 0, (ULONG)rows, bits, information, usage) == rows;
    unsigned rop = (operation >> RASTER_OPERATION_INDEX_SHIFT) & RASTER_OPERATION_INDEX_MASK;
    StretchBlit blit = {dc,       bitmap,       canvas_height, x,    y,
                        dw,       dh,           image_height,  rows, source_x,
                        source_y, source_width, source_height, rop,  lx_pm_rop_uses_pattern(rop),
                        0};
    RECTL device = {left, canvas_height - bottom, right, canvas_height - top};
    if (ok)
        ok = lx_pm_blit_rect(handle, &device, LX_PM_BLIT_READ, stretch_pel, &blit);
    lx_handle_release(&borrow);
    if (bitmap_handle)
        DeleteObject(bitmap_handle);
    if (!ok)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return source_height < 0 ? -source_height : source_height;
}

LONG SetDIBitsToDevice(ULONG dc, LONG x, LONG y, LONG width, LONG height, LONG sx, LONG sy,
                       ULONG first, ULONG count, PVOID bits, PVOID info, ULONG usage)
{
    size_t info_size, pixel_size;
    if (!open32FindDC(dc))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!open32DibBuffers(info, first, count, usage, &info_size, &pixel_size) || !bits || !count ||
        width < 0 || height < 0)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LONG image_width = (LONG)lx_guest_load_u32((uint8 *)info + WIN32_BITMAPINFOHEADER_WIDTH),
         rows = (LONG)lx_guest_load_u32((uint8 *)info + WIN32_BITMAPINFOHEADER_HEIGHT);
    ULONG total = (ULONG)(rows < 0 ? -rows : rows);
    if (count > total - first)
        count = total - first;
    /* The buffer holds one band of scan lines; transfer only its overlap with the source. */
    int64_t low = (int64_t)sy > (int64_t)first ? (int64_t)sy : (int64_t)first,
            high = (int64_t)sy + height;
    if (high > (int64_t)first + count)
        high = (int64_t)first + count;
    if ((int64_t)sx + width > image_width)
        width = sx < image_width ? image_width - sx : 0;
    if (low >= high || !width)
        return (LONG)count;
    int64_t skipped = rows < 0 ? low - sy : (int64_t)sy + height - high;
    if ((int64_t)y + skipped > INT32_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    uint8 *copy = malloc(info_size);
    if (!copy)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    memcpy(copy, info, info_size);
    lx_guest_store_u32(copy + WIN32_BITMAPINFOHEADER_HEIGHT,
                       rows < 0 ? (ULONG) - (LONG)count : count);
    LONG result =
        StretchDIBits(dc, x, (LONG)(y + skipped), width, (LONG)(high - low), sx,
                      (LONG)(low - first), width, (LONG)(high - low), bits, copy, usage, SRCCOPY);
    free(copy);
    return result ? (LONG)count : 0;
}
