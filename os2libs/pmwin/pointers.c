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
    unsigned hot_x, hot_y;
} PmPointer;
static HPOINTER current_pointer;
static LONG pointer_level;
static BOOL pointer_selected = TRUE; /* The host starts with its default arrow. */
LONG queryPointerLevel(void)
{
    return __atomic_load_n(&pointer_level, __ATOMIC_ACQUIRE);
}
static BOOL applyPointerVisibility(void)
{
    return lx_host_pointer_show(!queryPointerLevel() &&
                                __atomic_load_n(&pointer_selected, __ATOMIC_ACQUIRE));
}
static void release_pointer(void *resource)
{
    PmPointer *pointer = resource;
    PmPointer **link = &pointer->anchor->pointers;
    while (*link && *link != pointer)
        link = &(*link)->next;
    if (*link)
        *link = pointer->next;
    HPOINTER expected = pointer->handle;
    __atomic_compare_exchange_n(&current_pointer, &expected, 0, FALSE, __ATOMIC_ACQ_REL,
                                __ATOMIC_ACQUIRE);
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
    size_t header = size >= 14 && lx_guest_load_u16(bytes) == 0x4142 ? 14 : 0;
    unsigned hot_x = image ? lx_guest_load_u16((uint8 *)bytes + header + 6) : 0;
    unsigned hot_y = image ? lx_guest_load_u16((uint8 *)bytes + header + 8) : 0;
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
    pointer->hot_x = hot_x < image->width ? hot_x : 0;
    pointer->hot_y = hot_y < image->height ? image->height - 1 - hot_y : 0;
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
static PmPointerImage *system_icon(LONG id)
{
    PmPointerImage *image = calloc(1, sizeof *image);
    if (!image)
        return NULL;
    image->width = image->height = 32;
    image->pixels = calloc(32 * 32, sizeof *image->pixels);
    image->mask = malloc(32 * 32);
    if (!image->pixels || !image->mask)
    {
        pmPointerImageFree(image);
        return NULL;
    }
    memset(image->mask, 1, 32 * 32);
    static const uint8 information[] = {0x18, 0x18, 0, 0x38, 0x18, 0x18, 0x18, 0x3c};
    static const uint8 question[] = {0x3c, 0x66, 0x06, 0x0c, 0x18, 0, 0x18, 0};
    static const uint8 warning[] = {0x18, 0x18, 0x18, 0x18, 0x18, 0, 0x18, 0};
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
        {
            int dx = x - 16, dy = y - 16, radius = dx * dx + dy * dy;
            BOOL filled = id == 14   ? y >= 3 && y <= 29 && abs(dx) <= (30 - y) / 2
                          : id <= 14 ? radius <= 196
                                     : x >= 5 && x <= 26 && y >= 4 && y <= 27;
            if (!filled)
                continue;
            size_t at = (size_t)y * 32 + x;
            image->mask[at] = 0;
            uint32 color = id == 13               ? 0xc02020
                           : id == 14 || id == 20 ? 0xffdc40
                           : id > 14              ? 0xffffff
                                                  : 0x2040b0;
            if (id == 13 && y >= 8 && y <= 24 && (abs(dx - dy) <= 1 || abs(dx + dy) <= 1))
                color = 0xffffff;
            if ((id == 11 || id == 12 || id == 14) && x >= 8 && x < 24 && y >= 8 && y < 24)
            {
                const uint8 *glyph = id == 11 ? information : id == 12 ? question : warning;
                if (glyph[(23 - y) / 2] & (0x80 >> ((x - 8) / 2)))
                    color = id == 14 ? 0 : 0xffffff;
            }
            if (id > 14 && (x == 5 || x == 26 || y == 4 || y == 27 ||
                            (y >= 10 && y <= 22 && y % 4 == 2 && x >= 9 && x <= 22)))
                color = 0x404040;
            image->pixels[at] = color;
        }
    return image;
}
HPOINTER WinQuerySysPointer(HWND desktop, LONG id, BOOL copy)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || desktop != HWND_DESKTOP || !((id >= 1 && id <= 14) || (id >= 18 && id <= 22)))
        return 0;
    for (PmPointer *p = anchor->pointers; p && !copy; p = p->next)
        if (!p->copy && p->id == id)
            return p->handle;
    PmPointer *p = calloc(1, sizeof *p);
    if (!p)
        return 0;
    if ((id >= 10 && id <= 14) || id >= 19)
        p->image = system_icon(id);
    else
        p->host = lx_host_pointer_create(id == 18 ? LX_HOST_POINTER_ILLEGAL : id - 1);
    if (!p->host && !p->image)
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
        pmPointerImageFree(p->image);
        free(p);
        return 0;
    }
    p->next = anchor->pointers;
    anchor->pointers = p;
    return p->handle;
}
BOOL WinSetPointer(HWND desktop, HPOINTER handle)
{
    TRACE_NATIVE("WinSetPointer(%u, %u)", (uint)desktop, (uint)handle);
    if (desktop != HWND_DESKTOP || !getAnchorBlockNoHAB())
        return FALSE;
    if (!handle)
    {
        BOOL result = lx_host_pointer_set(NULL);
        if (result)
        {
            __atomic_store_n(&current_pointer, 0, __ATOMIC_RELEASE);
            __atomic_store_n(&pointer_selected, FALSE, __ATOMIC_RELEASE);
        }
        return result;
    }
    LxHandleBorrow borrow;
    if (!lx_handle_borrow(handle, LX_HANDLE_PM_POINTER, &borrow))
        return FALSE;
    PmPointer *pointer = borrow.resource;
    if (!pointer->host && pointer->image)
    {
        PmPointerImage *image = pointer->image;
        size_t count = (size_t)image->width * image->height;
        uint32 *pixels = malloc(count * sizeof *pixels);
        if (pixels)
        {
            for (unsigned y = 0; y < image->height; y++)
                for (unsigned x = 0; x < image->width; x++)
                {
                    size_t at = (size_t)(image->height - 1 - y) * image->width + x;
                    pixels[(size_t)y * image->width + x] =
                        image->mask[at] && !image->pixels[at] ? 0 : 0xff000000 | image->pixels[at];
                }
            pointer->host = lx_host_pointer_create_color(pixels, image->width, image->height,
                                                         pointer->hot_x, pointer->hot_y);
            free(pixels);
        }
    }
    BOOL result = pointer->host && lx_host_pointer_set(pointer->host);
    if (result)
    {
        __atomic_store_n(&current_pointer, handle, __ATOMIC_RELEASE);
        __atomic_store_n(&pointer_selected, TRUE, __ATOMIC_RELEASE);
        result = applyPointerVisibility();
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
    TRACE_NATIVE("WinShowPointer(%u, %u)", (uint)desktop, (uint)show);
    if (desktop != HWND_DESKTOP || !getAnchorBlockNoHAB())
        return FALSE;
    LONG previous = queryPointerLevel(), next;
    do
    {
        if (!show && previous == INT32_MAX)
            return FALSE;
        next = show ? (previous ? previous - 1 : 0) : previous + 1;
    } while (!__atomic_compare_exchange_n(&pointer_level, &previous, next, FALSE, __ATOMIC_ACQ_REL,
                                          __ATOMIC_ACQUIRE));
    return applyPointerVisibility();
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
static BOOL draw_pointer(HPS space, LONG x, LONG y, HPOINTER handle, ULONG flags)
{
    LxHandleBorrow borrow = {0};
    if (!lx_pm_valid_ps(space) || !lx_handle_borrow(handle, LX_HANDLE_PM_POINTER, &borrow))
        return FALSE;
    PmPointerImage *image = ((PmPointer *)borrow.resource)->image;
    BOOL result = image != NULL;
    for (unsigned row = 0; result && row < image->height; row++)
        for (unsigned column = 0; result && column < image->width; column++)
        {
            if ((flags & 1) && !((row + column) & 1))
                continue;
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
            uint32 pixel = (destination & (image->mask[at] ? 0xffffff : 0)) ^ image->pixels[at] ^
                           (flags & 2 ? 0xffffff : 0);
            result = lx_pm_raster_write(space, (LONG)px, (LONG)py, 1, 1, &pixel);
        }
    lx_handle_release(&borrow);
    return result;
}

BOOL lx_pm_pointer_draw(HPS space, LONG x, LONG y, HPOINTER handle)
{
    return draw_pointer(space, x, y, handle, 0);
}
BOOL WinDrawPointer(HPS space, LONG x, LONG y, HPOINTER handle, ULONG flags)
{
    return !(flags & ~3u) && draw_pointer(space, x, y, handle, flags);
}

HPOINTER WinQueryPointer(HWND desktop)
{
    return desktop == HWND_DESKTOP && getAnchorBlockNoHAB()
               ? __atomic_load_n(&current_pointer, __ATOMIC_ACQUIRE)
               : 0;
}
