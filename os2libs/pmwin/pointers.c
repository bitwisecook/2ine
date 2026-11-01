/* Public domain. No rights reserved. Wholly AI-authored. */
/* System cursor handles retain the host cursor and distinguish shared resources from copies. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "guest/abi.h"
typedef struct PmPointer
{
    struct PmPointer *next;
    AnchorBlock *anchor;
    HPOINTER handle;
    LONG id;
    BOOL copy;
    LxHostPointer *host;
    uint32_t *pixels;
    unsigned width, height, hot_x, hot_y;
} PmPointer;
static void release_pointer(void *resource)
{
    PmPointer *pointer = resource;
    PmPointer **link = &pointer->anchor->pointers;
    while (*link && *link != pointer)
        link = &(*link)->next;
    if (*link)
        *link = pointer->next;
    lx_host_pointer_destroy(pointer->host);
    free(pointer->pixels);
    free(pointer);
}
HPOINTER WinLoadPointer(HWND desktop, HMODULE module, ULONG id)
{
    TRACE_NATIVE("WinLoadPointer(%u, %u, %u)", (uint)desktop, (uint)module, (uint)id);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    uint32 size = 0;
    void *resource = NULL;
    if (!anchor)
        return 0;
    if (desktop != HWND_DESKTOP)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, 0);
    if (!GLoaderState.queryResourceSize || !GLoaderState.getResource ||
        !GLoaderState.freeResource || GLoaderState.queryResourceSize(module, 1, id, &size) ||
        GLoaderState.getResource(module, 1, id, &resource))
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_RESOURCE_NOT_FOUND, 0);
    const uint8 *p = resource;
    HPOINTER result = 0;
    PmPointer *pointer = NULL;
    /* A color icon/pointer has a monochrome mask followed by its color header.
       Bitmap-array resources select their first image. Offsets remain resource-relative. */
    size_t offset = size >= 14 && lx_guest_load_u16(p) == 0x4142 ? 14 : 0;
    if (size - offset < 32 ||
        (lx_guest_load_u16(p + offset) != 0x4943 && lx_guest_load_u16(p + offset) != 0x5043) ||
        lx_guest_load_u32(p + offset + 14) != 12)
        goto done;
    const uint8 *mask = p + offset;
    unsigned width = lx_guest_load_u16(mask + 18), twice_height = lx_guest_load_u16(mask + 20);
    if (!width || width > 256 || !twice_height || (twice_height & 1) || twice_height > 512 ||
        lx_guest_load_u16(mask + 22) != 1 || lx_guest_load_u16(mask + 24) != 1)
        goto done;
    unsigned height = twice_height / 2;
    size_t color_at = offset + 32;
    if (size - color_at < 26)
        goto done;
    const uint8 *color = p + color_at;
    if (lx_guest_load_u32(color + 14) != 12 || lx_guest_load_u16(color + 18) != width ||
        lx_guest_load_u16(color + 20) != height || lx_guest_load_u16(color + 22) != 1)
        goto done;
    unsigned depth = lx_guest_load_u16(color + 24);
    if (depth != 1 && depth != 4 && depth != 8 && depth != 24)
        goto done;
    unsigned colors = depth <= 8 ? 1u << depth : 0;
    size_t stride = ((size_t)width * depth + 31) / 32 * 4,
           mask_stride = ((size_t)width + 31) / 32 * 4;
    uint32 bits_at = lx_guest_load_u32(color + 10), mask_at = lx_guest_load_u32(mask + 10);
    if (colors * 3u > size - color_at - 26 || bits_at > size || stride * height > size - bits_at ||
        mask_at > size || mask_stride * twice_height > size - mask_at)
        goto done;
    pointer = calloc(1, sizeof *pointer);
    if (!pointer)
        goto done;
    pointer->pixels = malloc((size_t)width * height * 4);
    if (!pointer->pixels)
        goto done;
    const uint8 *palette = color + 26;
    for (unsigned y = 0; y < height; y++)
        for (unsigned x = 0; x < width; x++)
        {
            const uint8 *row = p + bits_at + y * stride;
            const uint8 *rgb;
            if (colors)
            {
                unsigned bit = x * depth;
                unsigned index = (row[bit / 8] >> (8 - depth - bit % 8)) & (colors - 1);
                rgb = palette + index * 3;
            }
            else
                rgb = row + x * 3;
            unsigned transparent =
                p[mask_at + (y + height) * mask_stride + x / 8] & (0x80 >> (x % 8));
            pointer->pixels[(size_t)(height - 1 - y) * width + x] =
                (transparent ? 0 : 0xff000000u) | (uint32)rgb[2] << 16 | (uint32)rgb[1] << 8 |
                rgb[0];
        }
    unsigned hot_x = lx_guest_load_u16(mask + 6), hot_y = lx_guest_load_u16(mask + 8);
    if (hot_x >= width || hot_y >= height)
        goto done;
    pointer->anchor = anchor;
    pointer->copy = TRUE;
    pointer->width = width;
    pointer->height = height;
    pointer->hot_x = hot_x;
    pointer->hot_y = height - 1 - hot_y;
    pointer->handle = lx_handle_create(LX_HANDLE_PM_POINTER, pointer, release_pointer);
    if (!pointer->handle)
        goto done;
    result = pointer->handle;
    pointer->next = anchor->pointers;
    anchor->pointers = pointer;
done:
    if (!result && pointer)
    {
        lx_host_pointer_destroy(pointer->host);
        free(pointer->pixels);
        free(pointer);
    }
    GLoaderState.freeResource(resource);
    return result;
}
void destroyPointers(AnchorBlock *anchor)
{
    while (anchor->pointers)
        lx_handle_close(anchor->pointers->handle, LX_HANDLE_PM_POINTER);
}
HPOINTER WinQuerySysPointer(HWND desktop, LONG id, BOOL copy)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || desktop != HWND_DESKTOP || !((id >= 1 && id <= 9) || id == 18))
        return 0;
    for (PmPointer *p = anchor->pointers; p && !copy; p = p->next)
        if (!p->copy && p->id == id)
            return p->handle;
    PmPointer *p = calloc(1, sizeof *p);
    if (!p)
        return 0;
    p->host = lx_host_pointer_create(id == 18 ? LX_HOST_POINTER_ILLEGAL : id - 1);
    if (!p->host)
    {
        free(p);
        return 0;
    }
    p->anchor = anchor;
    p->id = id;
    p->copy = copy;
    p->handle = lx_handle_create(LX_HANDLE_PM_POINTER, p, release_pointer);
    if (!p->handle)
    {
        lx_host_pointer_destroy(p->host);
        free(p);
        return 0;
    }
    p->next = anchor->pointers;
    anchor->pointers = p;
    return p->handle;
}
BOOL WinSetPointer(HWND desktop, HPOINTER handle)
{
    if (desktop != HWND_DESKTOP || !getAnchorBlockNoHAB())
        return FALSE;
    if (!handle)
        return lx_host_pointer_set(NULL);
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_PM_POINTER, &borrow))
        return FALSE;
    PmPointer *pointer = borrow.resource;
    if (!pointer->host && pointer->pixels)
        pointer->host = lx_host_pointer_create_color(
            pointer->pixels, pointer->width, pointer->height, pointer->hot_x, pointer->hot_y);
    BOOL result = pointer->host && lx_host_pointer_set(pointer->host);
    lx_handle_release(&borrow);
    return result;
}
BOOL WinDrawPointer(HPS ps, LONG x, LONG y, HPOINTER handle, ULONG flags)
{
    TRACE_NATIVE("WinDrawPointer(%u, %d, %d, %u, %u)", (uint)ps, (int)x, (int)y, (uint)handle,
                 (uint)flags);
    LxHandleBorrow borrow = {0};
    if ((flags & ~3u) || !lx_handle_borrow(handle, LX_HANDLE_PM_POINTER, &borrow))
        return FALSE;
    PmPointer *pointer = borrow.resource;
    BOOL result = FALSE;
    if (pointer->anchor == getAnchorBlockNoHAB() && pointer->pixels)
    {
        unsigned width = pointer->width, height = pointer->height;
        uint32_t *pixels = malloc((size_t)width * height * 4);
        if (pixels && lx_pm_raster_read(ps, x, y, width, height, pixels))
        {
            for (unsigned row = 0; row < height; row++)
                for (unsigned col = 0; col < width; col++)
                {
                    uint32_t color = pointer->pixels[(size_t)(height - 1 - row) * width + col];
                    if ((color >> 24) && (!(flags & 1) || ((row + col) & 1)))
                        pixels[(size_t)row * width + col] =
                            (color & 0xffffff) ^ (flags & 2 ? 0xffffff : 0);
                }
            result = lx_pm_raster_write(ps, x, y, width, height, pixels);
        }
        free(pixels);
    }
    lx_handle_release(&borrow);
    return result;
}
BOOL WinDestroyPointer(HPOINTER handle)
{
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_PM_POINTER, &borrow))
        return FALSE;
    BOOL copy = ((PmPointer *)borrow.resource)->copy;
    lx_handle_release(&borrow);
    return copy && lx_handle_close(handle, LX_HANDLE_PM_POINTER);
}

BOOL WinSetPointerPos(HWND desktop, LONG x, LONG y)
{
    return desktop == HWND_DESKTOP && getAnchorBlockNoHAB() &&
           lx_host_mouse_warp(x, desktop_window.h - 1 - y);
}
BOOL WinShowPointer(HWND desktop, BOOL show)
{
    return desktop == HWND_DESKTOP && getAnchorBlockNoHAB() && lx_host_pointer_show(show != 0);
}
BOOL WinShowCursor(HWND handle, BOOL show)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    Window *window = anchor ? getWindowFromHWND(anchor, handle) : NULL;
    /* Text carets are painted by their control; absent carets cannot be shown. */
    return window && !show;
}
