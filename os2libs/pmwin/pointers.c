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
    pointer->anchor = anchor;
    pointer->image = image;
    pointer->copy = TRUE;
    pointer->handle = lx_handle_create(LX_HANDLE_PM_POINTER, pointer, release_pointer);
    if (!pointer->handle)
    {
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
    LxHostPointer *host = ((PmPointer *)borrow.resource)->host;
    BOOL result = host && lx_host_pointer_set(host);
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
