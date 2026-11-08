/* Public domain. No rights reserved. Wholly AI-authored. */
/* OS/2 bitmap headers are byte-packed; resources retain absolute file offsets. */
#include "os2/include/private/export.h"
#include "os2/include/pmerr.h"
#include "os2/include/pmsei.h"
#include "os2/include/pmgpi.h"
#include "os2/ps/pmwin/internal.h"
#include "os2/ps/pmwin/graphics.h"
#include "emulator/abi.h"

ULONG GpiCreateBitmap(HPS ps, PVOID header, ULONG options, PVOID bits, PVOID info)
{
    TRACE_NATIVE("GpiCreateBitmap(%u, %p, %u, %p, %p)", (uint)ps, header, (uint)options, bits,
                 info);
    return lx_pm_bitmap_create_data(ps, header, options, bits, info);
}

ULONG GpiLoadBitmap(HPS ps, HMODULE module, ULONG id, LONG width, LONG height)
{
    TRACE_NATIVE("GpiLoadBitmap(%u, %u, %u, %d, %d)", (uint)ps, (uint)module, (uint)id, (int)width,
                 (int)height);
    return lx_pm_bitmap_load(ps, module, id, width, height);
}

BOOL GpiDeleteBitmap(ULONG bitmap)
{
    return lx_pm_bitmap_delete(bitmap);
}

ULONG GpiSetBitmap(HPS ps, ULONG bitmap)
{
    return lx_pm_bitmap_select(ps, bitmap);
}

BOOL GpiQueryBitmapParameters(ULONG handle, PVOID data)
{
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    BOOL ok = bitmap && data;
    if (ok)
    {
        uint8 *p = data;
        lx_guest_store_u32(p, 12);
        lx_guest_store_u16(p + 4, bitmap->width);
        lx_guest_store_u16(p + 6, bitmap->height);
        lx_guest_store_u16(p + 8, bitmap->planes);
        lx_guest_store_u16(p + 10, bitmap->depth);
    }
    lx_handle_release(&borrow);
    return ok;
}

BOOL GpiQueryBitmapInfoHeader(ULONG handle, PVOID data)
{
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    BOOL ok = bitmap && data && lx_guest_load_u32(data) == 64;
    if (ok)
    {
        uint8 *p = data;
        memset(p, 0, 64);
        lx_guest_store_u32(p, 64);
        lx_guest_store_u32(p + 4, bitmap->width);
        lx_guest_store_u32(p + 8, bitmap->height);
        lx_guest_store_u16(p + 12, bitmap->planes);
        lx_guest_store_u16(p + 14, bitmap->depth);
        lx_guest_store_u32(p + 20, (uint32)(((size_t)bitmap->width * bitmap->depth + 31) / 32 * 4 *
                                            bitmap->height));
    }
    lx_handle_release(&borrow);
    return ok;
}

BOOL GpiQueryDeviceBitmapFormats(HPS ps, LONG count, PLONG output)
{
    if (!lx_pm_valid_ps(ps) || count < 0 || (count && !output))
        return FALSE;
    /* Plane and depth pairs, preferred format first; a display offers nothing above its depth. */
    const unsigned depth = GProcessState.display_depth ? GProcessState.display_depth : 24;
    LONG formats[10] = {1, (LONG)depth};
    LONG available = 2;
    for (unsigned i = 0, others[] = {8, 4, 1}; i < 3; i++)
        if (others[i] < depth)
        {
            formats[available++] = 1;
            formats[available++] = (LONG)others[i];
        }
    for (LONG i = 0; i < count; i++)
        output[i] = i < available ? formats[i] : 0;
    return TRUE;
}

static uint32 rop_pixel(uint32 p, uint32 s, uint32 d, unsigned rop)
{
    uint32 value = 0;
    for (unsigned i = 0; i < 8; i++)
        if (rop & (1u << i))
            value |= (i & 4 ? p : ~p) & (i & 2 ? s : ~s) & (i & 1 ? d : ~d);
    return value & 0xffffff;
}

static LONG bitmap_blt(HPS destination, HPS source, PmBitmap *bitmap, LONG count, PPOINTL points,
                       LONG rop, ULONG options)
{
    /* Each bit pair differing only in the source term matches when a ROP ignores the source. */
    const BOOL uses_source = ((rop ^ (rop >> 2)) & 0x33) != 0;
    if (!lx_pm_valid_ps(destination) || (uses_source && !bitmap && !lx_pm_valid_ps(source)) ||
        ((count != 3 && count != 4) && (uses_source || count != 2)) || !points || rop < 0 ||
        rop > 255 || options > 2)
        return 0;
    int64_t w = (int64_t)points[1].x - points[0].x, h = (int64_t)points[1].y - points[0].y;
    int64_t sw = count == 4 ? (int64_t)points[3].x - points[2].x : w,
            sh = count == 4 ? (int64_t)points[3].y - points[2].y : h;
    if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0 || w > 16384 || h > 16384 || sw > 16384 ||
        sh > 16384 || w * h > 16777216 || sw * sh > 16777216)
        return 0;
    uint32 *src = calloc((size_t)sw * sh, 4), *dst = calloc((size_t)w * h, 4);
    if (!src || !dst)
    {
        free(src);
        free(dst);
        return 0;
    }
    BOOL ok = TRUE;
    if (!uses_source)
        ok = TRUE;
    else if (bitmap)
    {
        if (points[2].x < 0 || points[2].y < 0 || (uint64)points[2].x + sw > bitmap->width ||
            (uint64)points[2].y + sh > bitmap->height)
            ok = FALSE;
        else
            for (LONG row = 0; row < sh; row++)
                memcpy(src + (size_t)row * sw,
                       bitmap->pixels + (size_t)(points[2].y + row) * bitmap->width + points[2].x,
                       (size_t)sw * 4);
    }
    else
        ok = lx_pm_raster_read(source, points[2].x, points[2].y, (unsigned)sw, (unsigned)sh, src);
    PresentationSpace *ps = lx_pm_font_space(destination);
    BOOL transparent = ps && (ps->back_mix == 18 || ps->back_mix == 19);
    if (ok && (transparent || (rop != 0xcc && rop != 0 && rop != 0xff)))
    {
        PmBitmap *target = ps->bitmap_lease.resource;
        LONG target_width = target ? (LONG)target->width : ps->window ? ps->window->w : 0;
        LONG target_height = target ? (LONG)target->height : ps->window ? ps->window->h : 0;
        LONG left = points[0].x > 0 ? points[0].x : 0;
        LONG bottom = points[0].y > 0 ? points[0].y : 0;
        LONG right = points[1].x < target_width ? points[1].x : target_width;
        LONG top = points[1].y < target_height ? points[1].y : target_height;
        if (left < right && bottom < top)
        {
            size_t width = (size_t)(right - left), height = (size_t)(top - bottom);
            uint32 *visible = malloc(width * height * sizeof *visible);
            ok = visible && lx_pm_raster_read(destination, left, bottom, (unsigned)width,
                                              (unsigned)height, visible);
            if (ok)
                for (size_t row = 0; row < height; row++)
                    memcpy(dst + ((size_t)((int64_t)bottom - points[0].y) + row) * w +
                               ((int64_t)left - points[0].x),
                           visible + row * width, width * sizeof *visible);
            free(visible);
        }
    }
    LONG background = lx_pm_resolve_color(destination, ps->back_color);
    if (ok)
    {
        for (LONG y = 0; y < h && ok; y++)
            for (LONG x = 0; x < w && ok; x++)
            {
                size_t index = (size_t)y * w + x;
                uint32 source_pixel = src[(size_t)(y * sh / h) * sw + x * sw / w];
                if (uses_source && ((ps->back_mix == 18 && source_pixel == (uint32)background) ||
                                    (ps->back_mix == 19 && dst[index] != (uint32)background)))
                    continue;
                uint32 pattern = 0;
                BOOL foreground;
                if (((rop ^ (rop >> 4)) & 15) &&
                    !lx_pm_pattern_pixel(destination, points[0].x + x, points[0].y + y, &pattern,
                                         &foreground))
                {
                    ok = FALSE;
                    break;
                }
                dst[index] = rop_pixel(pattern, source_pixel, dst[index], (unsigned)rop);
            }
        if (ok)
            ok = lx_pm_raster_write(destination, points[0].x, points[0].y, (unsigned)w, (unsigned)h,
                                    dst);
    }
    free(dst);
    free(src);
    return ok ? 1 : 0;
}

LONG GpiBitBlt(HPS destination, HPS source, LONG count, PPOINTL points, LONG rop, ULONG options)
{
    TRACE_NATIVE("GpiBitBlt(%u, %u, %d, %d,%d - %d,%d)", (uint)destination, (uint)source,
                 (int)count, points ? (int)points[0].x : 0, points ? (int)points[0].y : 0,
                 points ? (int)points[1].x : 0, points ? (int)points[1].y : 0);
    return bitmap_blt(destination, source, NULL, count, points, rop, options);
}

LONG GpiWCBitBlt(HPS destination, ULONG handle, LONG count, PPOINTL points, LONG rop, ULONG options)
{
    PresentationSpace *ps = lx_pm_font_space(destination);
    TRACE_NATIVE("GpiWCBitBlt(%u, %u, %d, %p, %X, %u)", (uint)destination, (uint)handle, (int)count,
                 points, (uint)rop, (uint)options);
    if (!ps || !points || (count != 3 && count != 4))
        return 0;
    POINTL device[4];
    memcpy(device, points, (size_t)count * sizeof *device);
    if (!lx_pm_transform_point(ps, &device[0], TRUE) ||
        !lx_pm_transform_point(ps, &device[1], TRUE))
        return 0;
    /* Unlike GpiBitBlt, the target rectangle's upper-right corner is inclusive here. */
    device[1].x++;
    device[1].y++;
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = handle ? lx_pm_bitmap(handle, &borrow) : NULL;
    /* A pattern-only ROP passes no bitmap; bitmap_blt rejects any other ROP without a source. */
    LONG result =
        bitmap || !handle ? bitmap_blt(destination, 0, bitmap, count, device, rop, options) : 0;
    lx_handle_release(&borrow);
    return result;
}

BOOL GpiSetBitmapId(HPS handle, ULONG bitmap, LONG id)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || id < 1 || id > 254 || ps->logical_fonts[id].defined || ps->bitmap_ids[id].resource)
        return FALSE;
    LxHandleBorrow borrow = {0};
    if (!lx_pm_bitmap(bitmap, &borrow))
        return FALSE;
    ps->bitmap_ids[id] = borrow;
    return TRUE;
}

ULONG GpiQueryBitmapHandle(HPS handle, LONG id)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || id < 1 || id > 254)
        return 0;
    PmBitmap *bitmap = ps->bitmap_ids[id].resource;
    return bitmap ? bitmap->handle : 0;
}

/* Scan-line transfer uses caller-selected format; dimensions and RGB table are outputs. */
LONG GpiQueryBitmapBits(HPS handle, LONG start, LONG scans, PVOID bits, PVOID information)
{
    TRACE_NATIVE("GpiQueryBitmapBits(%u,%d,%d,%p,%p)", handle, start, scans, bits, information);
    PresentationSpace *ps = lx_pm_font_space(handle);
    PmBitmap *bitmap = ps ? ps->bitmap_lease.resource : NULL;
    if (!bitmap || !information || start < 0 || scans < 0 || (ULONG)start > bitmap->height)
        return -1;
    uint8 *info = information;
    unsigned fixed = lx_guest_load_u32(info);
    if (fixed != 12 && fixed != 64)
        return -1;
    unsigned planes = lx_guest_load_u16(info + (fixed == 12 ? 8 : 12));
    unsigned depth = lx_guest_load_u16(info + (fixed == 12 ? 10 : 14));
    if (planes != 1 || (depth != 1 && depth != 4 && depth != 8 && depth != 24 && depth != 32))
        return -1;
    unsigned rows =
        (ULONG)scans > bitmap->height - (ULONG)start ? bitmap->height - (ULONG)start : (ULONG)scans;
    if (rows && !bits)
        return -1;
    unsigned colors = depth <= 8 ? 1u << depth : 0;
    uint32 palette[256] = {0};
    static const uint32 vga[16] = {0,        0x800000, 0x008000, 0x808000, 0x000080, 0x800080,
                                   0x008080, 0xc0c0c0, 0x808080, 0xff0000, 0x00ff00, 0xffff00,
                                   0x0000ff, 0xff00ff, 0x00ffff, 0xffffff};
    for (unsigned i = 0; i < colors; i++)
        palette[i] = bitmap->palette_count && depth >= bitmap->depth
                         ? (i < bitmap->palette_count ? bitmap->palette[i] : 0)
                     : depth == 1 ? (i ? 0xffffff : 0)
                     : depth == 4 ? vga[i]
                                  : ((i >> 5) * 255 / 7) << 16 | (((i >> 2) & 7) * 255 / 7) << 8 |
                                        (i & 3) * 85;
    size_t stride = ((size_t)bitmap->width * depth + 31) / 32 * 4;
    memset(info, 0, fixed + colors * (fixed == 12 ? 3 : 4));
    lx_guest_store_u32(info, fixed);
    if (fixed == 12)
    {
        lx_guest_store_u16(info + 4, bitmap->width);
        lx_guest_store_u16(info + 6, bitmap->height);
    }
    else
    {
        lx_guest_store_u32(info + 4, bitmap->width);
        lx_guest_store_u32(info + 8, bitmap->height);
        lx_guest_store_u32(info + 20, stride * bitmap->height);
        lx_guest_store_u32(info + 32, colors);
    }
    lx_guest_store_u16(info + (fixed == 12 ? 8 : 12), 1);
    lx_guest_store_u16(info + (fixed == 12 ? 10 : 14), depth);
    for (unsigned i = 0; i < colors; i++)
    {
        uint8 *entry = info + fixed + i * (fixed == 12 ? 3 : 4);
        entry[0] = (uint8)palette[i];
        entry[1] = (uint8)(palette[i] >> 8);
        entry[2] = (uint8)(palette[i] >> 16);
    }
    if (rows)
        memset(bits, 0, stride * rows);
    for (unsigned y = 0; y < rows; y++)
        for (unsigned x = 0; x < bitmap->width; x++)
        {
            uint32 rgb = bitmap->pixels[((size_t)start + y) * bitmap->width + x];
            uint8 *row = (uint8 *)bits + y * stride;
            if (colors)
            {
                unsigned best = 0, distance = UINT32_MAX;
                for (unsigned i = 0; i < colors; i++)
                {
                    int r = (int)((rgb >> 16) & 255) - (int)((palette[i] >> 16) & 255);
                    int g = (int)((rgb >> 8) & 255) - (int)((palette[i] >> 8) & 255);
                    int b = (int)(rgb & 255) - (int)(palette[i] & 255);
                    unsigned next = (unsigned)(r * r + g * g + b * b);
                    if (next < distance)
                    {
                        best = i;
                        distance = next;
                        if (!next)
                            break;
                    }
                }
                unsigned bit = x * depth;
                row[bit / 8] |= (uint8)(best << (8 - depth - bit % 8));
            }
            else
            {
                uint8 *p = row + x * (depth / 8);
                p[0] = (uint8)rgb;
                p[1] = (uint8)(rgb >> 8);
                p[2] = (uint8)(rgb >> 16);
            }
        }
    return (LONG)rows;
}

LONG GpiDrawBits(HPS handle, PVOID bits, PVOID information, LONG count, PPOINTL points, LONG rop,
                 ULONG options)
{
    TRACE_NATIVE("GpiDrawBits(%u,%p,%p,%d,%p,%d,%u)", handle, bits, information, count, points, rop,
                 options);
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !bits || !information || !points || (count != 3 && count != 4))
        return 0;
    TRACE_NATIVE("GpiDrawBits points={%d,%d;%d,%d;%d,%d;%d,%d}", points[0].x, points[0].y,
                 points[1].x, points[1].y, points[2].x, points[2].y, count == 4 ? points[3].x : 0,
                 count == 4 ? points[3].y : 0);
    if (options > 2)
    {
        WinSetErrorInfo(PMERR_FUNCTION_NOT_SUPPORTED, 0, NULL);
        return 0;
    }
    POINTL p[4];
    memcpy(p, points, (size_t)count * sizeof *p);
    if (!lx_pm_transform_point(ps, &p[0], TRUE) || !lx_pm_transform_point(ps, &p[1], TRUE))
        return 0;
    int64_t width = (int64_t)p[1].x - p[0].x, height = (int64_t)p[1].y - p[0].y;
    int64_t sw = count == 4 ? (int64_t)p[3].x - p[2].x : width;
    int64_t sh = count == 4 ? (int64_t)p[3].y - p[2].y : height;
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || width * height > 16777216 ||
        sw <= 0 || sh <= 0 || sw > 16384 || sh > 16384)
        return 0;
    ULONG source = lx_pm_bitmap_create_data(handle, information, 4, bits, information);
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = source ? lx_pm_bitmap(source, &borrow) : NULL;
    LONG result = 0;
    PmBitmap scaled = {.width = (unsigned)width, .height = (unsigned)height};
    if (!bitmap || p[2].x < 0 || p[2].y < 0 || (uint64)p[2].x + sw > bitmap->width ||
        (uint64)p[2].y + sh > bitmap->height)
        goto done;
    scaled.pixels = malloc((size_t)width * height * sizeof *scaled.pixels);
    if (!scaled.pixels)
        goto done;
    size_t stride = ((size_t)bitmap->width * bitmap->depth + 31) / 32 * 4;
    for (unsigned y = 0; y < (unsigned)height; y++)
        for (unsigned x = 0; x < (unsigned)width; x++)
        {
            unsigned x0 = (unsigned)((uint64)x * sw / width),
                     y0 = (unsigned)((uint64)y * sh / height);
            unsigned x1 =
                options == 2 ? x0 + 1 : (unsigned)(((uint64)(x + 1) * sw + width - 1) / width);
            unsigned y1 =
                options == 2 ? y0 + 1 : (unsigned)(((uint64)(y + 1) * sh + height - 1) / height);
            uint32 value = options == 1 ? UINT32_MAX : 0;
            for (unsigned sy = y0; sy < y1; sy++)
                for (unsigned sx = x0; sx < x1; sx++)
                {
                    unsigned ix = (unsigned)p[2].x + sx, iy = (unsigned)p[2].y + sy;
                    uint32 pixel;
                    if (bitmap->depth <= 8)
                    {
                        unsigned bit = ix * bitmap->depth;
                        const uint8 *row = (uint8 *)bits + iy * stride;
                        pixel = (row[bit / 8] >> (8 - bitmap->depth - bit % 8)) &
                                ((1u << bitmap->depth) - 1);
                    }
                    else
                        pixel = bitmap->pixels[(size_t)iy * bitmap->width + ix];
                    value = options == 1 ? value & pixel : value | pixel;
                }
            if (bitmap->depth <= 8)
                value = bitmap->palette[value];
            scaled.pixels[(size_t)y * width + x] = value;
        }
    p[2] = (POINTL){0, 0};
    result = bitmap_blt(handle, 0, &scaled, 3, p, rop, 2);
done:
    TRACE_NATIVE("GpiDrawBits result=%d source=%ux%u depth=%u target=%lldx%lld", result,
                 bitmap ? bitmap->width : 0, bitmap ? bitmap->height : 0,
                 bitmap ? bitmap->depth : 0, (long long)width, (long long)height);
    free(scaled.pixels);
    lx_handle_release(&borrow);
    if (source)
        lx_pm_bitmap_delete(source);
    return result;
}
