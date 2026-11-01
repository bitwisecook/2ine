/* Public domain. No rights reserved. Wholly AI-authored. */
/* System cursor handles retain the host cursor and distinguish shared resources from copies. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/pointer_image.h"
typedef struct PmPointer
{
    struct PmPointer *next;
    AnchorBlock *anchor;
    HPOINTER handle;
    LONG id;
    BOOL copy;
    LxHostPointer *host;
    PmPointerImage *image;
    uint32 *pixels;
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
    pmPointerImageFree(pointer->image);
    free(pointer);
}
void destroyPointers(AnchorBlock *anchor)
{
    while (anchor->pointers)
        lx_handle_close(anchor->pointers->handle, LX_HANDLE_PM_POINTER);
}
HPOINTER WinLoadPointer(HWND desktop, HMODULE module, ULONG id)
{
    TRACE_NATIVE("WinLoadPointer(%u, %u, %u)", (uint)desktop, (uint)module, (uint)id);
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return 0;
    if (desktop != HWND_DESKTOP || !GLoaderState.queryResourceSize || !GLoaderState.getResource)
        return 0;
    void *bytes = NULL;
    ULONG size;
    if (GLoaderState.queryResourceSize(module, 1, id, &size) || size > 16 * 1024 * 1024 ||
        GLoaderState.getResource(module, 1, id, &bytes))
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_RESOURCE_NOT_FOUND, 0);
    PmPointerImage *image = pmPointerImage(bytes, size);
    size_t offset = size >= 14 && lx_guest_load_u16(bytes) == 0x4142 ? 14 : 0;
    unsigned hot_x = size >= offset + 10 ? lx_guest_load_u16((uint8 *)bytes + offset + 6) : 0;
    unsigned hot_y = size >= offset + 10 ? lx_guest_load_u16((uint8 *)bytes + offset + 8) : 0;
    if (GLoaderState.freeResource)
        GLoaderState.freeResource(bytes);
    if (!image)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_RESOURCE_FORMAT, 0);
    PmPointer *pointer = calloc(1, sizeof *pointer);
    if (!pointer)
    {
        pmPointerImageFree(image);
        return 0;
    }
    pointer->pixels = malloc((size_t)image->width * image->height * sizeof *pointer->pixels);
    if (!pointer->pixels || hot_x >= image->width || hot_y >= image->height)
    {
        free(pointer->pixels);
        pmPointerImageFree(image);
        free(pointer);
        return 0;
    }
    pointer->width = image->width;
    pointer->height = image->height;
    pointer->hot_x = hot_x;
    pointer->hot_y = image->height - 1 - hot_y;
    for (unsigned y = 0; y < image->height; y++)
        for (unsigned x = 0; x < image->width; x++)
        {
            size_t source = (size_t)y * image->width + x;
            size_t target = (size_t)(image->height - 1 - y) * image->width + x;
            pointer->pixels[target] =
                image->pixels[source] | (image->mask[source] ? 0 : 0xff000000u);
        }
    pointer->anchor = anchor;
    pointer->image = image;
    pointer->copy = TRUE;
    pointer->handle = lx_handle_create(LX_HANDLE_PM_POINTER, pointer, release_pointer);
    if (!pointer->handle)
    {
        free(pointer->pixels);
        pmPointerImageFree(image);
        free(pointer);
        return 0;
    }
    pointer->next = anchor->pointers;
    anchor->pointers = pointer;
    return pointer->handle;
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
    BOOL result = copy && lx_handle_close_guarded(handle, LX_HANDLE_PM_POINTER, 1, NULL) == 1;
    lx_handle_release(&borrow);
    return result;
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

BOOL lx_pm_pointer_dimensions(HPOINTER handle, LONG *width, LONG *height)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_PM_POINTER, &borrow))
        return FALSE;
    PmPointerImage *image = ((PmPointer *)borrow.resource)->image;
    BOOL result = image != NULL;
    if (result)
    {
        *width = (LONG)image->width;
        *height = (LONG)image->height;
    }
    lx_handle_release(&borrow);
    return result;
}
BOOL lx_pm_pointer_draw(HPS space, LONG x, LONG y, HPOINTER handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_pm_valid_ps(space) || !lx_handle_borrow(handle, LX_HANDLE_PM_POINTER, &borrow))
        return FALSE;
    PmPointerImage *image = ((PmPointer *)borrow.resource)->image;
    BOOL result = image != NULL;
    for (unsigned row = 0; result && row < image->height; row++)
        for (unsigned column = 0; result && column < image->width; column++)
        {
            int64_t px = (int64_t)x + column, py = (int64_t)y + row;
            if (px < INT32_MIN || px > INT32_MAX || py < INT32_MIN || py > INT32_MAX)
            {
                result = FALSE;
                break;
            }
            uint32 destination;
            if (!lx_pm_raster_read(space, (LONG)px, (LONG)py, 1, 1, &destination))
                continue;
            size_t at = (size_t)row * image->width + column;
            uint32 pixel = (destination & (image->mask[at] ? 0xffffff : 0)) ^ image->pixels[at];
            result = lx_pm_raster_write(space, (LONG)px, (LONG)py, 1, 1, &pixel);
        }
    lx_handle_release(&borrow);
    return result;
}
