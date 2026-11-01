/* Public domain. No rights reserved. Wholly AI-authored. */
/* DIB rows use DWORD padding; positive heights retain bottom-up bitmap order. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/graphics.h"
BOOL open32DibBuffers(PVOID information, ULONG start, ULONG count, ULONG usage,
                      size_t *information_bytes, size_t *pixel_bytes)
{
    if (!information || lx_guest_load_u32(information) != 40 || usage)
    {
        open32SetError(usage ? 50 : 87);
        return FALSE;
    }
    const uint8 *info = information;
    unsigned depth = lx_guest_load_u16(info + 14);
    *information_bytes = 40;
    *pixel_bytes = 0;
    if (!depth)
        return TRUE;
    LONG width = (LONG)lx_guest_load_u32(info + 4), height = (LONG)lx_guest_load_u32(info + 8);
    uint64_t rows = height < 0 ? -(int64_t)height : height;
    if (width < 1 || width > 16384 || !rows || rows > 16384 || start > rows ||
        lx_guest_load_u16(info + 12) != 1 || lx_guest_load_u32(info + 16) ||
        (depth != 1 && depth != 4 && depth != 8 && depth != 24 && depth != 32))
    {
        open32SetError(87);
        return FALSE;
    }
    unsigned colors = lx_guest_load_u32(info + 32);
    if (depth <= 8)
    {
        if (colors > (1u << depth))
        {
            open32SetError(87);
            return FALSE;
        }
        *information_bytes += (colors ? colors : 1u << depth) * 4;
    }
    else if (colors)
    {
        open32SetError(50);
        return FALSE;
    }
    uint64_t stride = (((uint64_t)width * depth + 31) / 32) * 4;
    if (count > rows - start)
        count = (ULONG)(rows - start);
    *pixel_bytes = (size_t)(stride * count);
    return TRUE;
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
        open32SetError(6);
        return 0;
    }
    if (!open32DibBuffers(information, start, count, usage, &info_bytes, &pixel_bytes))
        return 0;
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    if (!bitmap || bitmap->selected)
    {
        lx_handle_release(&borrow);
        open32SetError(bitmap ? 87 : 6);
        return 0;
    }
    uint8 *info = information;
    unsigned depth = lx_guest_load_u16(info + 14);
    if (!depth)
    {
        memset(info, 0, 40);
        lx_guest_store_u32(info, 40);
        lx_guest_store_u32(info + 4, bitmap->width);
        lx_guest_store_u32(info + 8, bitmap->height);
        lx_guest_store_u16(info + 12, 1);
        lx_guest_store_u16(info + 14, bitmap->depth);
        lx_guest_store_u32(info + 20,
                           ((bitmap->width * bitmap->depth + 31) / 32) * 4 * bitmap->height);
        if (bitmap->depth <= 8)
            lx_guest_store_u32(info + 32, 1u << bitmap->depth);
        LONG result = (LONG)bitmap->height;
        lx_handle_release(&borrow);
        return result;
    }
    LONG height = (LONG)lx_guest_load_u32(info + 8);
    if (lx_guest_load_u32(info + 4) != bitmap->width ||
        (uint64_t)(height < 0 ? -(int64_t)height : height) != bitmap->height)
    {
        lx_handle_release(&borrow);
        open32SetError(87);
        return 0;
    }
    uint32 colors[256] = {0};
    unsigned color_count = (unsigned)((info_bytes - 40) / 4);
    open32BitmapPalette(depth, colors);
    for (unsigned i = 0; i < color_count; i++)
    {
        if (depth == bitmap->depth && bitmap->palette_count)
            colors[i] = i < bitmap->palette_count ? bitmap->palette[i] : 0;
        lx_guest_store_u32(info + 40 + i * 4, colors[i]);
    }
    unsigned stride = ((bitmap->width * depth + 31) / 32) * 4;
    lx_guest_store_u32(info + 20, stride * bitmap->height);
    lx_guest_store_u32(info + 32, color_count);
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
                if (depth <= 8)
                {
                    unsigned index = nearest(colors, color_count, rgb), bit = x * depth;
                    output[bit / 8] |= (uint8)(index << (8 - depth - bit % 8));
                }
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
        open32SetError(6);
        return 0;
    }
    if (!open32DibBuffers(information, start, count, usage, &info_bytes, &pixel_bytes))
        return 0;
    const uint8 *info = information;
    unsigned depth = lx_guest_load_u16(info + 14);
    if (!depth || (pixel_bytes && !bits))
    {
        open32SetError(87);
        return 0;
    }
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    if (!bitmap || bitmap->selected)
    {
        lx_handle_release(&borrow);
        open32SetError(bitmap ? 87 : 6);
        return 0;
    }
    LONG height = (LONG)lx_guest_load_u32(info + 8);
    if (lx_guest_load_u32(info + 4) != bitmap->width ||
        (uint64_t)(height < 0 ? -(int64_t)height : height) != bitmap->height)
    {
        lx_handle_release(&borrow);
        open32SetError(87);
        return 0;
    }
    if (count > bitmap->height - start)
        count = bitmap->height - start;
    if (!count)
    {
        lx_handle_release(&borrow);
        return 0;
    }
    uint32 palette[256] = {0}, destination_colors[256];
    unsigned colors = (unsigned)((info_bytes - 40) / 4);
    for (unsigned i = 0; i < colors; i++)
        palette[i] = lx_guest_load_u32(info + 40 + i * 4) & 0xFFFFFF;
    open32BitmapPalette(bitmap->depth, destination_colors);
    if (bitmap->depth == depth && colors)
        memcpy(destination_colors, palette, sizeof palette);
    else if (bitmap->palette_count)
        memcpy(destination_colors, bitmap->palette, sizeof bitmap->palette);
    uint32 *decoded = malloc((size_t)bitmap->width * count * 4);
    if (!decoded)
    {
        lx_handle_release(&borrow);
        open32SetError(8);
        return 0;
    }
    BOOL valid = TRUE;
    unsigned stride = ((bitmap->width * depth + 31) / 32) * 4;
    for (unsigned row = 0; row < count && valid; row++)
    {
        const uint8 *source = (const uint8 *)bits + (size_t)row * stride;
        for (unsigned x = 0; x < bitmap->width; x++)
        {
            uint32 rgb;
            if (depth <= 8)
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
            else
            {
                const uint8 *pixel = source + x * (depth / 8);
                rgb = (uint32)pixel[2] << 16 | (uint32)pixel[1] << 8 | pixel[0];
            }
            if (bitmap->depth <= 8)
                rgb = destination_colors[nearest(destination_colors, 1u << bitmap->depth, rgb)];
            decoded[(size_t)row * bitmap->width + x] = rgb;
        }
    }
    if (valid)
    {
        if (bitmap->depth <= 8)
        {
            bitmap->palette_count = 1u << bitmap->depth;
            memcpy(bitmap->palette, destination_colors, sizeof bitmap->palette);
        }
        for (unsigned row = 0; row < count; row++)
        {
            unsigned target = height < 0 ? bitmap->height - 1 - start - row : start + row;
            memcpy(bitmap->pixels + (size_t)target * bitmap->width,
                   decoded + (size_t)row * bitmap->width, (size_t)bitmap->width * 4);
        }
    }
    free(decoded);
    lx_handle_release(&borrow);
    if (!valid)
    {
        open32SetError(87);
        return 0;
    }
    return (LONG)count;
}
