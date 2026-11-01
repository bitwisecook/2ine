/* Public domain. No rights reserved. Wholly AI-authored. */
/* Selected bitmap lifetimes are pinned by the presentation space. Pixels are bottom-up RGB. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
static void destroy_bitmap(void *resource)
{
    PmBitmap *bitmap = resource;
    AnchorBlock *anchor = bitmap->anchor;
    PmBitmap **slot = &anchor->bitmaps;
    while (*slot && *slot != bitmap)
        slot = &(*slot)->next;
    if (*slot)
        *slot = bitmap->next;
    free(bitmap->pixels);
    free(bitmap);
}
PmBitmap *lx_pm_bitmap(ULONG handle, LxHandleBorrow *borrow)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || !lx_handle_borrow(handle, LX_HANDLE_PM_BITMAP, borrow))
        return NULL;
    PmBitmap *bitmap = borrow->resource;
    if (bitmap->anchor != anchor)
    {
        lx_handle_release(borrow);
        return NULL;
    }
    return bitmap;
}
ULONG lx_pm_bitmap_create(unsigned width, unsigned height, unsigned planes, unsigned depth)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || !width || !height || width > 16384 || height > 16384 ||
        (uint64_t)width * height > 16777216 || planes != 1 ||
        (depth != 1 && depth != 4 && depth != 8 && depth != 24 && depth != 32))
        return 0;
    PmBitmap *bitmap = calloc(1, sizeof *bitmap);
    if (!bitmap)
        return 0;
    bitmap->pixels = calloc((size_t)width * height, sizeof *bitmap->pixels);
    if (!bitmap->pixels)
    {
        free(bitmap);
        return 0;
    }
    bitmap->width = width;
    bitmap->height = height;
    bitmap->planes = planes;
    bitmap->depth = depth;
    bitmap->anchor = anchor;
    bitmap->handle = lx_handle_create(LX_HANDLE_PM_BITMAP, bitmap, destroy_bitmap);
    if (!bitmap->handle)
    {
        free(bitmap->pixels);
        free(bitmap);
        return 0;
    }
    bitmap->next = anchor->bitmaps;
    anchor->bitmaps = bitmap;
    return bitmap->handle;
}
static int unused_bitmap(void *resource)
{
    return ((PmBitmap *)resource)->selected == 0;
}
BOOL lx_pm_bitmap_delete(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    BOOL result =
        bitmap && lx_handle_close_guarded(handle, LX_HANDLE_PM_BITMAP, 1, unused_bitmap) == 1;
    lx_handle_release(&borrow);
    return result;
}
ULONG lx_pm_bitmap_select(HPS handle, ULONG bitmap)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || ps->window)
        return (ULONG)-1;
    PmBitmap *previous = ps->bitmap_lease.resource;
    if (previous && previous->handle == bitmap)
        return bitmap;
    LxHandleBorrow next = {0};
    if (bitmap)
    {
        PmBitmap *value = lx_pm_bitmap(bitmap, &next);
        if (!value || value->selected)
        {
            lx_handle_release(&next);
            return (ULONG)-1;
        }
        value->selected++;
    }
    ULONG result = previous ? previous->handle : 0;
    if (previous)
        previous->selected--;
    lx_handle_release(&ps->bitmap_lease);
    ps->bitmap_lease = next;
    return result;
}
void lx_pm_bitmap_cleanup(void *context)
{
    AnchorBlock *anchor = context;
    while (anchor->bitmaps)
        lx_handle_close(anchor->bitmaps->handle, LX_HANDLE_PM_BITMAP);
}
BOOL lx_pm_raster_read(HPS handle, LONG x, LONG y, unsigned width, unsigned height,
                       uint32_t *pixels)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !pixels || x < 0 || y < 0 || !width || !height || width > 16384 || height > 16384)
        return FALSE;
    PmBitmap *bitmap = ps->bitmap_lease.resource;
    if (bitmap)
    {
        if ((ULONG)x > bitmap->width || width > bitmap->width - x || (ULONG)y > bitmap->height ||
            height > bitmap->height - y)
            return FALSE;
        for (unsigned row = 0; row < height; row++)
            memcpy(pixels + (size_t)row * width,
                   bitmap->pixels + (size_t)(y + row) * bitmap->width + x, (size_t)width * 4);
        return TRUE;
    }
    Window *window = ps->window;
    if (!window || !window->heavy.window || (ULONG)x > (ULONG)window->w ||
        width > (ULONG)window->w - x || (ULONG)y > (ULONG)window->h ||
        height > (ULONG)window->h - y)
        return FALSE;
    LxHostRect origin;
    calcHeavyweightRect(window, &origin);
    LxHostRect rect = {origin.x + x, origin.y + origin.h - y - (int)height, (int)width,
                       (int)height};
    uint32_t *top = malloc((size_t)width * height * 4);
    if (!top)
        return FALSE;
    BOOL ok = lx_host_window_read(window->heavy.window, &rect, top);
    if (ok)
        for (unsigned row = 0; row < height; row++)
            memcpy(pixels + (size_t)row * width, top + (size_t)(height - 1 - row) * width,
                   (size_t)width * 4);
    free(top);
    return ok;
}
BOOL lx_pm_raster_write(HPS handle, LONG x, LONG y, unsigned width, unsigned height,
                        const uint32_t *pixels)
{
    PresentationSpace *ps = lx_pm_font_space(handle);
    if (!ps || !pixels || !width || !height || width > 16384 || height > 16384)
        return FALSE;
    PmBitmap *bitmap = ps->bitmap_lease.resource;
    if (bitmap)
    {
        for (unsigned row = 0; row < height; row++)
        {
            int64_t dy = (int64_t)y + row;
            if (dy < 0 || dy >= bitmap->height)
                continue;
            for (unsigned column = 0; column < width; column++)
            {
                int64_t dx = (int64_t)x + column;
                if (dx >= 0 && dx < bitmap->width && lx_pm_clip_point(handle, (LONG)dx, (LONG)dy))
                    bitmap->pixels[(size_t)dy * bitmap->width + dx] =
                        pixels[(size_t)row * width + column];
            }
        }
        return TRUE;
    }
    Window *window = ps->window;
    if (!window || !window->heavy.window)
        return FALSE;
    LxHostRect origin;
    calcHeavyweightRect(window, &origin);
    LxHostRect rect = {origin.x + x, origin.y + origin.h - y - (int)height, (int)width,
                       (int)height};
    uint32_t *top = malloc((size_t)width * height * 4);
    if (!top)
        return FALSE;
    for (unsigned row = 0; row < height; row++)
        memcpy(top + (size_t)row * width, pixels + (size_t)(height - 1 - row) * width,
               (size_t)width * 4);
    PmRegion visible = {0};
    BOOL ok = lx_pm_window_clip(handle, NULL, &visible);
    for (size_t i = 0; ok && i < visible.count; i++)
    {
        RECTL bounds = {0, 0, window->w, window->h}, part;
        if (!WinIntersectRect(0, &part, &bounds, &visible.rects[i]))
            continue;
        LxHostRect host = {origin.x + part.xLeft, origin.y + origin.h - part.yTop,
                           part.xRight - part.xLeft, part.yTop - part.yBottom};
        ok = lx_host_window_image(window->heavy.window, &host, &rect, top);
    }
    free(visible.rects);
    free(top);
    return ok;
}

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
    if (width > 16384 || height > 16384 || width * height > 16777216)
        return FALSE;
    uint32_t *pixels = malloc(width * height * sizeof *pixels);
    if (!pixels)
        return FALSE;
    BOOL result = lx_pm_raster_read(handle, rect.xLeft, rect.yBottom, width, height, pixels);
    if (result)
    {
        for (size_t i = 0; i < width * height; i++)
            pixels[i] ^= 0xffffff;
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
    if (!bitmap || !point || (flags & ~0x1fu))
        goto done;
    RECTL rect = source ? *source : (RECTL){0, 0, (LONG)bitmap->width, (LONG)bitmap->height};
    if (rect.xLeft < 0 || rect.yBottom < 0 || rect.xRight > (LONG)bitmap->width ||
        rect.yTop > (LONG)bitmap->height || rect.xRight <= rect.xLeft || rect.yTop <= rect.yBottom)
        goto done;
    LONG sw = rect.xRight - rect.xLeft, sh = rect.yTop - rect.yBottom;
    LONG width = flags & 4 ? point[1].x - point[0].x : sw;
    LONG height = flags & 4 ? point[1].y - point[0].y : sh;
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        (uint64_t)width * height > 16777216)
        goto done;
    uint32_t *pixels = malloc((size_t)width * height * 4);
    if (!pixels)
        goto done;
    uint32_t *previous = flags & 16 ? malloc((size_t)width * height * 4) : NULL;
    if ((flags & 16) &&
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
            if (bitmap->depth == 1 && !(flags & 8) && fore >= 0 && back >= 0)
                rgb = rgb ? (uint32_t)back : (uint32_t)fore;
            if (flags & 1)
                rgb ^= 0xffffff;
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
