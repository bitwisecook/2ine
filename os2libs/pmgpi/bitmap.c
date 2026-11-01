/* Public domain. No rights reserved. Wholly AI-authored. */
/* OS/2 bitmap headers are byte-packed; resources retain absolute file offsets. */
#include "os2libs/os2native.h"
#include "os2libs/pmgpi.h"
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "guest/abi.h"
typedef struct
{
    unsigned width, height, planes, depth, fixed, palette_entry, colors, compression;
    size_t stride, bytes;
} BitmapLayout;
static BOOL bitmap_layout(const uint8 *p, BitmapLayout *b)
{
    if (!p)
        return FALSE;
    memset(b, 0, sizeof *b);
    b->fixed = lx_guest_load_u32(p);
    if (b->fixed == 12)
    {
        b->width = lx_guest_load_u16(p + 4);
        b->height = lx_guest_load_u16(p + 6);
        b->planes = lx_guest_load_u16(p + 8);
        b->depth = lx_guest_load_u16(p + 10);
        b->palette_entry = 3;
    }
    else if (b->fixed == 40 || b->fixed == 64)
    {
        b->width = lx_guest_load_u32(p + 4);
        b->height = lx_guest_load_u32(p + 8);
        b->planes = lx_guest_load_u16(p + 12);
        b->depth = lx_guest_load_u16(p + 14);
        b->compression = lx_guest_load_u32(p + 16);
        b->palette_entry = 4;
        b->colors = lx_guest_load_u32(p + 32);
    }
    else
        return FALSE;
    if (!b->width || !b->height || b->width > 16384 || b->height > 16384 ||
        (uint64)b->width * b->height > 16777216 || b->planes != 1 ||
        (b->depth != 1 && b->depth != 4 && b->depth != 8 && b->depth != 24 && b->depth != 32))
        return FALSE;
    if (b->depth <= 8)
    {
        unsigned total = 1u << b->depth;
        if (!b->colors)
            b->colors = total;
        if (b->colors > total)
            return FALSE;
    }
    else if (b->colors)
        return FALSE;
    b->stride = ((size_t)b->width * b->depth + 31) / 32 * 4;
    b->bytes = b->stride * b->height;
    return b->compression == 0;
}
static BOOL bitmap_decode(PmBitmap *bitmap, const BitmapLayout *b, const uint8 *bits,
                          const uint8 *palette)
{
    if (!bitmap || !bits || (b->colors && !palette))
        return FALSE;
    for (unsigned y = 0; y < b->height; y++)
    {
        const uint8 *row = bits + (size_t)y * b->stride;
        for (unsigned x = 0; x < b->width; x++)
        {
            uint32 rgb;
            if (b->depth <= 8)
            {
                unsigned bit = x * b->depth, index = (row[bit / 8] >> (8 - b->depth - (bit % 8))) &
                                                     ((1u << b->depth) - 1);
                if (index >= b->colors)
                    return FALSE;
                const uint8 *c = palette + index * b->palette_entry;
                rgb = (uint32)c[2] << 16 | (uint32)c[1] << 8 | c[0];
            }
            else
            {
                const uint8 *c = row + x * (b->depth / 8);
                rgb = (uint32)c[2] << 16 | (uint32)c[1] << 8 | c[0];
            }
            bitmap->pixels[(size_t)y * bitmap->width + x] = rgb;
        }
    }
    return TRUE;
}
ULONG GpiCreateBitmap(HPS ps, PVOID header, ULONG options, PVOID bits, PVOID info)
{
    TRACE_NATIVE("GpiCreateBitmap(%u, %p, %u, %p, %p)", (uint)ps, header, (uint)options, bits,
                 info);
    BitmapLayout layout;
    if (!lx_pm_valid_ps(ps) || !header ||
        (lx_guest_load_u32(header) != 12 && lx_guest_load_u32(header) != 64) || (options & ~4u) ||
        !bitmap_layout(header, &layout) || ((options & 4) && (!bits || !info)))
        return 0;
    ULONG handle = lx_pm_bitmap_create(layout.width, layout.height, layout.planes, layout.depth);
    if (!handle)
        return 0;
    if (options & 4)
    {
        BitmapLayout source;
        LxHandleBorrow borrow = {0};
        PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
        BOOL ok = bitmap_layout(info, &source) && source.width == layout.width &&
                  source.height == layout.height &&
                  bitmap_decode(bitmap, &source, bits, (const uint8 *)info + source.fixed);
        lx_handle_release(&borrow);
        if (!ok)
        {
            lx_pm_bitmap_delete(handle);
            return 0;
        }
    }
    return handle;
}
ULONG GpiLoadBitmap(HPS ps, HMODULE module, ULONG id, LONG width, LONG height)
{
    TRACE_NATIVE("GpiLoadBitmap(%u, %u, %u, %d, %d)", (uint)ps, (uint)module, (uint)id, (int)width,
                 (int)height);
    if (!lx_pm_valid_ps(ps) || width < 0 || height < 0 || !GLoaderState.queryResourceSize ||
        !GLoaderState.getResource)
        return 0;
    uint32 size = 0;
    void *resource = NULL;
    if (GLoaderState.queryResourceSize(module, 2, id, &size) || size < 26 ||
        GLoaderState.getResource(module, 2, id, &resource))
        return 0;
    const uint8 *p = resource;
    size_t offset = lx_guest_load_u16(p) == 0x4142 ? 14 : 0;
    ULONG result = 0;
    if (size - offset < 26 || lx_guest_load_u16(p + offset) != 0x4d42)
        goto done;
    uint32 fixed = lx_guest_load_u32(p + offset + 14);
    if (fixed > size - offset - 14 || (fixed != 12 && fixed != 40 && fixed != 64))
        goto done;
    BitmapLayout b;
    if (!bitmap_layout(p + offset + 14, &b) ||
        (size_t)b.colors * b.palette_entry > size - offset - 14 - b.fixed)
        goto done;
    uint32 bits = lx_guest_load_u32(p + offset + 10);
    if (bits > size || b.bytes > size - bits)
        goto done;
    result = lx_pm_bitmap_create(b.width, b.height, b.planes, b.depth);
    if (result)
    {
        LxHandleBorrow borrow = {0};
        PmBitmap *bitmap = lx_pm_bitmap(result, &borrow);
        BOOL decoded = bitmap_decode(bitmap, &b, p + bits, p + offset + 14 + b.fixed);
        lx_handle_release(&borrow);
        if (!decoded)
        {
            lx_pm_bitmap_delete(result);
            result = 0;
        }
    }
    if (result &&
        ((width && (unsigned)width != b.width) || (height && (unsigned)height != b.height)))
    {
        unsigned w = width ? (unsigned)width : b.width, h = height ? (unsigned)height : b.height;
        ULONG scaled = lx_pm_bitmap_create(w, h, 1, b.depth);
        LxHandleBorrow old = {0}, next = {0};
        PmBitmap *src = lx_pm_bitmap(result, &old),
                 *dst = scaled ? lx_pm_bitmap(scaled, &next) : NULL;
        if (src && dst)
            for (unsigned y = 0; y < h; y++)
                for (unsigned x = 0; x < w; x++)
                    dst->pixels[(size_t)y * w + x] =
                        src->pixels[(size_t)(y * b.height / h) * b.width + x * b.width / w];
        lx_handle_release(&next);
        lx_handle_release(&old);
        lx_pm_bitmap_delete(result);
        result = scaled;
    }
done:
    if (GLoaderState.freeResource)
        GLoaderState.freeResource(resource);
    return result;
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
    const LONG formats[] = {1, 24, 1, 8, 1, 4, 1, 1};
    for (LONG i = 0; i < count; i++)
        output[i] = i < 8 ? formats[i] : 0;
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
    if (!lx_pm_valid_ps(destination) || (!bitmap && !lx_pm_valid_ps(source)) ||
        (count != 3 && count != 4) || !points || rop < 0 || rop > 255 || options > 2)
        return 0;
    int64_t w = (int64_t)points[1].x - points[0].x, h = (int64_t)points[1].y - points[0].y;
    int64_t sw = count == 4 ? (int64_t)points[3].x - points[2].x : w,
            sh = count == 4 ? (int64_t)points[3].y - points[2].y : h;
    if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0 || w > 16384 || h > 16384 || sw > 16384 ||
        sh > 16384 || w * h > 16777216 || sw * sh > 16777216)
        return 0;
    uint32 *src = malloc((size_t)sw * sh * 4), *dst = calloc((size_t)w * h, 4);
    if (!src || !dst)
    {
        free(src);
        free(dst);
        return 0;
    }
    BOOL ok = TRUE;
    if (bitmap)
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
    if (ok && rop != 0xcc && rop != 0 && rop != 0xff &&
        !lx_pm_raster_read(destination, points[0].x, points[0].y, (unsigned)w, (unsigned)h, dst))
        ok = FALSE;
    PresentationSpace *ps = lx_pm_font_space(destination);
    uint32 pattern = ps ? (uint32)lx_pm_resolve_color(destination, ps->color) : 0;
    if (ok)
    {
        for (LONG y = 0; y < h; y++)
            for (LONG x = 0; x < w; x++)
                dst[(size_t)y * w + x] =
                    rop_pixel(pattern, src[(size_t)(y * sh / h) * sw + x * sw / w],
                              dst[(size_t)y * w + x], (unsigned)rop);
        ok = lx_pm_raster_write(destination, points[0].x, points[0].y, (unsigned)w, (unsigned)h,
                                dst);
    }
    free(dst);
    free(src);
    return ok ? 1 : 0;
}
LONG GpiBitBlt(HPS destination, HPS source, LONG count, PPOINTL points, LONG rop, ULONG options)
{
    return bitmap_blt(destination, source, NULL, count, points, rop, options);
}
LONG GpiWCBitBlt(HPS destination, ULONG handle, LONG count, PPOINTL points, LONG rop, ULONG options)
{
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    LONG result = bitmap ? bitmap_blt(destination, 0, bitmap, count, points, rop, options) : 0;
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
