/* Public domain. No rights reserved. Wholly AI-authored. */
/* OS/2 bitmap headers are byte-packed; resources retain absolute file offsets. */
#include "os2libs/os2native.h"
#include "os2libs/pmgpi.h"
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "guest/abi.h"
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
    PresentationSpace *ps = lx_pm_font_space(destination);
    BOOL transparent = ps && (ps->back_mix == 18 || ps->back_mix == 19);
    if (ok && (transparent || (rop != 0xcc && rop != 0 && rop != 0xff)) &&
        !lx_pm_raster_read(destination, points[0].x, points[0].y, (unsigned)w, (unsigned)h, dst))
        ok = FALSE;
    LONG background = lx_pm_resolve_color(destination, ps->back_color);
    if (ok)
    {
        for (LONG y = 0; y < h && ok; y++)
            for (LONG x = 0; x < w && ok; x++)
            {
                size_t index = (size_t)y * w + x;
                uint32 source_pixel = src[(size_t)(y * sh / h) * sw + x * sw / w];
                if ((ps->back_mix == 18 && source_pixel == (uint32)background) ||
                    (ps->back_mix == 19 && dst[index] != (uint32)background))
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
    return bitmap_blt(destination, source, NULL, count, points, rop, options);
}
LONG GpiWCBitBlt(HPS destination, ULONG handle, LONG count, PPOINTL points, LONG rop, ULONG options)
{
    PresentationSpace *ps = lx_pm_font_space(destination);
    if (!ps || !points || (count != 3 && count != 4))
        return 0;
    POINTL device[4];
    memcpy(device, points, (size_t)count * sizeof *device);
    if (!lx_pm_transform_point(ps, &device[0], TRUE) ||
        !lx_pm_transform_point(ps, &device[1], TRUE))
        return 0;
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    LONG result = bitmap ? bitmap_blt(destination, 0, bitmap, count, device, rop, options) : 0;
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
