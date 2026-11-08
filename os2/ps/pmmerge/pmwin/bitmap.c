/* Public domain. No rights reserved. Wholly AI-authored. */
/* Selected bitmap lifetimes are pinned by the presentation space. Pixels are bottom-up RGB. */
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "os2/ps/pmmerge/pmwin/graphics.h"
#include "emulator/abi.h"

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
    bitmap->palette_count = b->colors;
    for (unsigned i = 0; i < b->colors; i++)
    {
        const uint8 *c = palette + i * b->palette_entry;
        bitmap->palette[i] = (uint32)c[2] << 16 | (uint32)c[1] << 8 | c[0];
    }
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

ULONG lx_pm_bitmap_create_data(HPS ps, PVOID header, ULONG options, PVOID bits, PVOID info)
{
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

ULONG lx_pm_bitmap_load(HPS ps, HMODULE module, ULONG id, LONG width, LONG height)
{
    if (!lx_pm_valid_ps(ps) || width < 0 || height < 0 || !GProcessState.queryResourceSize ||
        !GProcessState.getResource)
        return 0;
    uint32 size = 0;
    void *resource = NULL;
    if (GProcessState.queryResourceSize(module, 2, id, &size) || size < 26 ||
        GProcessState.getResource(module, 2, id, &resource))
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
        {
            dst->palette_count = src->palette_count;
            memcpy(dst->palette, src->palette, sizeof dst->palette);
            for (unsigned y = 0; y < h; y++)
                for (unsigned x = 0; x < w; x++)
                    dst->pixels[(size_t)y * w + x] =
                        src->pixels[(size_t)(y * b.height / h) * b.width + x * b.width / w];
        }
        lx_handle_release(&next);
        lx_handle_release(&old);
        lx_pm_bitmap_delete(result);
        result = scaled;
    }
done:
    if (GProcessState.freeResource)
        GProcessState.freeResource(resource);
    return result;
}

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
        (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16 && depth != 24 &&
         depth != 32))
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

static ULONG checkbox_bitmap(void)
{
    /* SBMP_CHECKBOXES is a four-column, three-row atlas. The top row's
       first and last cells are the normal unchecked and checked boxes. */
    const unsigned cell = 13, width = cell * 4, height = cell * 3;
    ULONG handle = lx_pm_bitmap_create(width, height, 1, 24);
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    if (!bitmap)
        return 0;
    for (unsigned y = 0; y < height; y++)
        for (unsigned x = 0; x < width; x++)
        {
            unsigned column = x / cell, row = y / cell, px = x % cell, py = y % cell;
            BOOL pressed = column == 1 || column == 2, checked = column >= 2;
            uint32 color = 0xcccccc;
            if (row == 1)
            {
                int dx = (int)px - 6, dy = (int)py - 6, radius = dx * dx + dy * dy;
                if (radius <= 25)
                    color = radius >= 16 ? 0x404040 : checked && radius <= 4 ? 0 : 0xffffff;
            }
            else if (px >= 1 && px <= 11 && py >= 1 && py <= 11)
            {
                color = px == 1 || py == 11   ? 0x404040
                        : px == 11 || py == 1 ? 0xffffff
                        : pressed             ? 0xcccccc
                                              : 0xffffff;
                if (checked && px >= 3 && px <= 9 && py >= 3 && py <= 9)
                {
                    if (row == 0 || (px <= 5 && (py == 7 - px || py == 8 - px)) ||
                        (px >= 5 && (py == px - 3 || py == px - 2)))
                        color = 0;
                }
            }
            bitmap->pixels[(size_t)y * width + x] = color;
        }
    lx_handle_release(&borrow);
    return handle;
}

ULONG WinGetSysBitmap(HWND desktop, ULONG id)
{
    TRACE_NATIVE("WinGetSysBitmap(%u, %u)", (uint)desktop, (uint)id);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return 0;
    if (desktop != 1 || (id != 6 && id != 18 && id != 19 && id != 48))
    {
        lxHostThreadState()->pm_error = desktop != 1 ? PMERR_INVALID_HWND : PMERR_INVALID_PARM;
        return 0;
    }
    if (id == 48)
        return checkbox_bitmap();
    ULONG handle = lx_pm_bitmap_create(9, 9, 1, 1);
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    if (!bitmap)
        return 0;
    for (unsigned y = 0; y < 9; y++)
        for (unsigned x = 0; x < 9; x++)
        {
            int marked;
            if (id == 6)
                marked = (x >= 1 && x <= 3 && y == 5 - x) || (x >= 3 && x <= 7 && y == x - 1);
            else
                marked = x == 0 || y == 0 || x == 8 || y == 8 || (y == 4 && x >= 2 && x <= 6) ||
                         (id == 18 && x == 4 && y >= 2 && y <= 6);
            bitmap->pixels[y * 9 + x] = marked ? 0 : 0xffffff;
        }
    lx_handle_release(&borrow);
    return handle;
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
    if (!ps || ps->window || lx_pm_printer_kind(ps->dc))
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
    lx_pm_boundary(ps, x, y, width, height);
    if (ps->gpi.initialized && !ps->gpi.display_enabled)
        return TRUE;
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
