/* Public domain. No rights reserved. Wholly AI-authored. */
/* WinInvertRect and WinDrawBitmap: rectangle inversion and bitmap drawing on a PS. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"
#include "emulator/abi.h"

BOOL WinInvertRect(HPS handle, PRECTL input)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !input)
        return FALSE;
    PmBitmap *bitmap = ps->bitmap_lease.resource;
    Window *window = ps->window;
    if (!bitmap && !window)
        return FALSE;
    RECTL bounds = {0, 0, bitmap ? (LONG)bitmap->width : window->w,
                    bitmap ? (LONG)bitmap->height : window->h},
          rect;
    if (!WinIntersectRect(0, &rect, &bounds, input))
        return TRUE;
    size_t width = rect.xRight - rect.xLeft, height = rect.yTop - rect.yBottom;
    if (width > LX_PM_RASTER_MAX_DIMENSION || height > LX_PM_RASTER_MAX_DIMENSION ||
        width * height > LX_PM_RASTER_MAX_PELS)
        return FALSE;
    uint32_t *pixels = malloc(width * height * sizeof *pixels);
    if (!pixels)
        return FALSE;
    BOOL result = lx_pm_raster_read(handle, rect.xLeft, rect.yBottom, width, height, pixels);
    if (result)
    {
        for (size_t i = 0; i < width * height; i++)
            pixels[i] ^= RGB_WHITE;
        result = lx_pm_raster_write(handle, rect.xLeft, rect.yBottom, width, height, pixels);
    }
    free(pixels);
    return result;
}

BOOL WinDrawBitmap(HPS destination, ULONG handle, PRECTL source, PPOINTL point, LONG foreground,
                   LONG background, ULONG flags)
{
    TRACE_NATIVE("WinDrawBitmap(%u, %u, %p, %p, %d, %d, %u)", (unsigned)destination,
                 (unsigned)handle, source, point, (int)foreground, (int)background,
                 (unsigned)flags);
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    BOOL result = FALSE;
    if (!bitmap || !point ||
        (flags & ~(unsigned)(DBM_INVERT | DBM_HALFTONE | DBM_STRETCH | DBM_IMAGEATTRS | DBM_XOR)))
        goto done;
    RECTL rect = source ? *source : (RECTL){0, 0, (LONG)bitmap->width, (LONG)bitmap->height};
    if (rect.xLeft < 0 || rect.yBottom < 0 || rect.xRight > (LONG)bitmap->width ||
        rect.yTop > (LONG)bitmap->height || rect.xRight <= rect.xLeft || rect.yTop <= rect.yBottom)
        goto done;
    LONG sw = rect.xRight - rect.xLeft, sh = rect.yTop - rect.yBottom;
    LONG width = flags & DBM_STRETCH ? point[1].x - point[0].x : sw;
    LONG height = flags & DBM_STRETCH ? point[1].y - point[0].y : sh;
    if (width <= 0 || height <= 0 || width > LX_PM_RASTER_MAX_DIMENSION ||
        height > LX_PM_RASTER_MAX_DIMENSION || (uint64_t)width * height > LX_PM_RASTER_MAX_PELS)
        goto done;
    uint32_t *pixels = malloc((size_t)width * height * 4);
    if (!pixels)
        goto done;
    uint32_t *previous = flags & DBM_XOR ? malloc((size_t)width * height * 4) : NULL;
    if ((flags & DBM_XOR) &&
        (!previous || !lx_pm_raster_read(destination, point->x, point->y, width, height, previous)))
    {
        free(previous);
        free(pixels);
        goto done;
    }
    LONG fore = lx_pm_resolve_color(destination, foreground),
         back = lx_pm_resolve_color(destination, background);
    for (LONG y = 0; y < height; y++)
        for (LONG x = 0; x < width; x++)
        {
            uint32_t rgb =
                bitmap->pixels[(size_t)(rect.yBottom + (int64_t)y * sh / height) * bitmap->width +
                               rect.xLeft + (int64_t)x * sw / width];
            if (bitmap->depth == 1 && !(flags & DBM_IMAGEATTRS) && fore >= 0 && back >= 0)
                rgb = rgb ? (uint32_t)back : (uint32_t)fore;
            if (flags & DBM_INVERT)
                rgb ^= RGB_WHITE;
            if (previous)
                rgb ^= previous[(size_t)y * width + x];
            pixels[(size_t)y * width + x] = rgb;
        }
    result = lx_pm_raster_write(destination, point->x, point->y, width, height, pixels);
    free(previous);
    free(pixels);
done:
    lx_handle_release(&borrow);
    return result;
}
