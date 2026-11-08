/* Public domain. No rights reserved. Wholly AI-authored. */
/* System cursor handles retain the host cursor and distinguish shared resources from copies. */
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/pointer_image.h"
#include "host/fs.h"

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
    ULONG info_mask, info_color;
    PmPointerImage *shape; /* Queryable image of a pointer the host draws itself. */
} PmPointer;

static HPOINTER current_pointer;
static LONG pointer_level;
static BOOL pointer_selected = TRUE; /* The host starts with its default arrow. */
static HPOINTER shared_system_pointers[23];

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
    if (pointer->anchor)
    {
        PmPointer **link = &pointer->anchor->pointers;
        while (*link && *link != pointer)
            link = &(*link)->next;
        if (*link)
            *link = pointer->next;
    }
    HPOINTER expected = pointer->handle;
    __atomic_compare_exchange_n(&current_pointer, &expected, 0, FALSE, __ATOMIC_ACQ_REL,
                                __ATOMIC_ACQUIRE);
    if (pointer->info_mask)
        lx_pm_bitmap_delete(pointer->info_mask);
    if (pointer->info_color)
        lx_pm_bitmap_delete(pointer->info_color);
    lx_host_pointer_destroy(pointer->host);
    pmPointerImageFree(pointer->image);
    pmPointerImageFree(pointer->shape);
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

static HPOINTER system_pointer(AnchorBlock *anchor, LONG id, BOOL copy)
{
    PmPointer *p = calloc(1, sizeof *p);
    if (!p)
        return 0;
    if ((id >= 10 && id <= 14) || id >= 19)
        p->image = system_icon(id);
    if (((id >= 10 && id <= 14) || id >= 19) && !p->image)
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
    if (anchor)
    {
        p->next = anchor->pointers;
        anchor->pointers = p;
    }
    return p->handle;
}

HPOINTER lx_pm_system_pointer(LONG id)
{
    if (!((id >= 1 && id <= 14) || (id >= 18 && id <= 22)))
        return 0;
    HPOINTER shared = __atomic_load_n(&shared_system_pointers[id], __ATOMIC_ACQUIRE);
    if (shared)
        return shared;
    HPOINTER created = system_pointer(NULL, id, FALSE);
    if (!created)
        return 0;
    if (__atomic_compare_exchange_n(&shared_system_pointers[id], &shared, created, FALSE,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return created;
    lx_handle_close(created, LX_HANDLE_PM_POINTER);
    return shared;
}

HPOINTER WinQuerySysPointer(HWND desktop, LONG id, BOOL copy)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || desktop != HWND_DESKTOP || !((id >= 1 && id <= 14) || (id >= 18 && id <= 22)))
        return 0;
    return copy ? system_pointer(anchor, id, TRUE) : lx_pm_system_pointer(id);
}

void destroySharedPointers(void)
{
    for (unsigned id = 0; id < sizeof shared_system_pointers / sizeof *shared_system_pointers; id++)
    {
        HPOINTER pointer = __atomic_exchange_n(&shared_system_pointers[id], 0, __ATOMIC_ACQ_REL);
        if (pointer)
            lx_handle_close(pointer, LX_HANDLE_PM_POINTER);
    }
}

LX_NATIVE_DESTRUCTOR(pmwin_system_pointers)
{
    destroySharedPointers();
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
    LxHostPointer *host = __atomic_load_n(&pointer->host, __ATOMIC_ACQUIRE), *created = NULL;
    if (!host && !pointer->image && pointer->id)
        created =
            lx_host_pointer_create(pointer->id == 18 ? LX_HOST_POINTER_ILLEGAL : pointer->id - 1);
    if (!host && pointer->image)
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
            created = lx_host_pointer_create_color(pixels, image->width, image->height,
                                                   pointer->hot_x, pointer->hot_y);
            free(pixels);
        }
    }
    if (created)
    {
        if (__atomic_compare_exchange_n(&pointer->host, &host, created, FALSE, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE))
            host = created;
        else
            lx_host_pointer_destroy(created);
    }
    BOOL result = host && lx_host_pointer_set(host);
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

static LONG pointer_clip[4];
static BOOL pointer_clipped;

void lx_pm_set_pointer_clip(const RECTL *bounds)
{
    if (bounds)
    {
        __atomic_store_n(&pointer_clip[0], bounds->xLeft, __ATOMIC_RELAXED);
        __atomic_store_n(&pointer_clip[1], bounds->yBottom, __ATOMIC_RELAXED);
        __atomic_store_n(&pointer_clip[2], bounds->xRight, __ATOMIC_RELAXED);
        __atomic_store_n(&pointer_clip[3], bounds->yTop, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&pointer_clipped, bounds != NULL, __ATOMIC_RELEASE);
}

BOOL queryPointerClip(RECTL *bounds)
{
    if (!__atomic_load_n(&pointer_clipped, __ATOMIC_ACQUIRE))
        return FALSE;
    *bounds = (RECTL){__atomic_load_n(&pointer_clip[0], __ATOMIC_RELAXED),
                      __atomic_load_n(&pointer_clip[1], __ATOMIC_RELAXED),
                      __atomic_load_n(&pointer_clip[2], __ATOMIC_RELAXED),
                      __atomic_load_n(&pointer_clip[3], __ATOMIC_RELAXED)};
    return TRUE;
}

BOOL lx_pm_clip_pointer(LONG *x, LONG *y)
{
    if (!__atomic_load_n(&pointer_clipped, __ATOMIC_ACQUIRE))
        return FALSE;
    LONG left = __atomic_load_n(&pointer_clip[0], __ATOMIC_RELAXED);
    LONG bottom = __atomic_load_n(&pointer_clip[1], __ATOMIC_RELAXED);
    LONG right = __atomic_load_n(&pointer_clip[2], __ATOMIC_RELAXED);
    LONG top = __atomic_load_n(&pointer_clip[3], __ATOMIC_RELAXED);
    LONG a = *x < left ? left : *x > right ? right : *x;
    LONG b = *y < bottom ? bottom : *y > top ? top : *y;
    BOOL moved = a != *x || b != *y;
    *x = a;
    *y = b;
    return moved;
}

BOOL WinSetPointerPos(HWND desktop, LONG x, LONG y)
{
    lx_pm_clip_pointer(&x, &y);
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

HPOINTER WinCreatePointerIndirect(HWND desktop, LXPOINTERINFO *info)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    LxHandleBorrow mb = {0}, cb = {0};
    if (!a || desktop != HWND_DESKTOP || !info || info->fPointer > 1 || !info->hbmPointer)
        return 0;
    PmBitmap *mask = lx_pm_bitmap(info->hbmPointer, &mb),
             *color = info->hbmColor ? lx_pm_bitmap(info->hbmColor, &cb) : NULL;
    unsigned width = mask ? mask->width : 0, height = color  ? color->height
                                                      : mask ? mask->height / 2
                                                             : 0;
    if (!mask || !width || !height || width > 1024 || height > 1024 ||
        (color && color->width != width) || mask->height < height || (info->hbmColor && !color) ||
        info->xHotspot < 0 || info->yHotspot < 0 || (ULONG)info->xHotspot >= width ||
        (ULONG)info->yHotspot >= height)
    {
        lx_handle_release(&cb);
        lx_handle_release(&mb);
        return 0;
    }
    PmPointer *p = calloc(1, sizeof *p);
    PmPointerImage *image = calloc(1, sizeof *image);
    if (image)
    {
        image->width = width;
        image->height = height;
        image->pixels = calloc((size_t)width * height, sizeof *image->pixels);
        image->mask = calloc((size_t)width * height, 1);
    }
    if (!p || !image || !image->pixels || !image->mask)
    {
        free(p);
        pmPointerImageFree(image);
        lx_handle_release(&cb);
        lx_handle_release(&mb);
        return 0;
    }
    for (unsigned y = 0; y < height; y++)
        for (unsigned x = 0; x < width; x++)
        {
            size_t dest = (size_t)(height - 1 - y) * width + x, src = (size_t)y * width + x;
            image->mask[dest] = (mask->pixels[src] & 0xffffff) != 0;
            image->pixels[dest] = color
                                      ? color->pixels[src] & 0xffffff
                                      : (mask->height >= height * 2
                                             ? mask->pixels[src + (size_t)height * width] & 0xffffff
                                             : 0);
        }
    lx_handle_release(&cb);
    lx_handle_release(&mb);
    p->anchor = a;
    p->image = image;
    p->hot_x = info->xHotspot;
    p->hot_y = height - 1 - info->yHotspot;
    p->copy = TRUE;
    p->handle = lx_handle_create(LX_HANDLE_PM_POINTER, p, release_pointer);
    if (!p->handle)
    {
        pmPointerImageFree(image);
        free(p);
        return 0;
    }
    p->next = a->pointers;
    a->pointers = p;
    return p->handle;
}

/* Host cursors expose no bitmap, so their shapes are described here. 'X' is black, '.' is
   white and a space is transparent; each entry gives its hot spot from the top left. */
static const struct
{
    LONG id;
    unsigned hot_x, hot_y;
    const char *rows[16];
} pointer_shapes[] = {
    {1,
     0,
     0,
     {"X               ", "XX              ", "X.X             ", "X..X            ",
      "X...X           ", "X....X          ", "X.....X         ", "X......X        ",
      "X.......X       ", "X....XXXXX      ", "X..X..X         ", "X.X X..X        ",
      "XX  X..X        ", "X    X..X       ", "     X..X       ", "      XX        "}},
    {2,
     4,
     8,
     {" XXX XXX        ", "    X           ", "    X           ", "    X           ",
      "    X           ", "    X           ", "    X           ", "    X           ",
      "    X           ", "    X           ", "    X           ", "    X           ",
      "    X           ", "    X           ", "    X           ", " XXX XXX        "}},
    {3,
     7,
     7,
     {" XXXXXXXXXXXXX  ", " X...........X  ", "  X.........X   ", "   X.......X    ",
      "    X.....X     ", "     X...X      ", "      X.X       ", "       X        ",
      "      X.X       ", "     X...X      ", "    X.....X     ", "   X.......X    ",
      "  X.........X   ", " X...........X  ", " XXXXXXXXXXXXX  ", "                "}},
    {4,
     7,
     7,
     {"       X        ", "      X.X       ", "     X...X      ", "    XX.XX       ",
      "      X         ", "  X   X   X     ", " X.X  X  X.X    ", "X...XXXXX...X   ",
      " X.X  X  X.X    ", "  X   X   X     ", "      X         ", "    XX.XX       ",
      "     X...X      ", "      X.X       ", "       X        ", "                "}},
    {5,
     7,
     7,
     {"       X        ", "      X.X       ", "     X...X      ", "      X.X       ",
      "      X.X       ", "  X   X.X   X   ", " XX XXX.XXX XX  ", "X.............X ",
      " XX XXX.XXX XX  ", "  X   X.X   X   ", "      X.X       ", "      X.X       ",
      "     X...X      ", "      X.X       ", "       X        ", "                "}},
    {6,
     7,
     7,
     {"XXXXX           ", "X..X            ", "X.X             ", "X..X            ",
      "XX X.X          ", "    X.X         ", "     X.X        ", "      X.X       ",
      "       X.X      ", "        X.X XX  ", "         X..X   ", "          X.X   ",
      "         X..X   ", "        XXXXX   ", "                ", "                "}},
    {7,
     7,
     7,
     {"        XXXXX   ", "         X..X   ", "          X.X   ", "         X..X   ",
      "       X.X XX   ", "      X.X       ", "     X.X        ", "    X.X         ",
      "   X.X          ", "XX X.X          ", " X..X           ", " X.X            ",
      " X..X           ", "XXXXX           ", "                ", "                "}},
    {8,
     7,
     7,
     {"                ", "                ", "                ", "                ",
      "   X       X    ", "  XX       XX   ", " X.XXXXXXXXX.X  ", "X.............X ",
      " X.XXXXXXXXX.X  ", "  XX       XX   ", "   X       X    ", "                ",
      "                ", "                ", "                ", "                "}},
    {18,
     7,
     7,
     {"     XXXXX      ", "   XX.....XX    ", "  X..XXXXX.X    ", " X.X    X.X.X   ",
      " X.X   X.X.X    ", "X.X   X.X X.X   ", "X.X  X.X  X.X   ", "X.X X.X   X.X   ",
      "X.XX.X    X.X   ", " X.X.X   X.X    ", " X.X.X  X.X     ", "  X.XXXX..X     ",
      "   XX.....XX    ", "     XXXXX      ", "                ", "                "}},
    {9,
     7,
     7,
     {"       X        ", "      X.X       ", "     X...X      ", "    XX.XX       ",
      "      X.X       ", "      X.X       ", "      X.X       ", "      X.X       ",
      "      X.X       ", "      X.X       ", "      X.X       ", "    XX.XX       ",
      "     X...X      ", "      X.X       ", "       X        ", "                "}},
};

static PmPointerImage *pointer_shape(PmPointer *p)
{
    if (p->shape)
        return p->shape;
    for (unsigned s = 0; s < sizeof pointer_shapes / sizeof *pointer_shapes; s++)
    {
        if (pointer_shapes[s].id != p->id)
            continue;
        PmPointerImage *image = calloc(1, sizeof *image);
        if (image)
        {
            image->width = image->height = 16;
            image->pixels = calloc(256, sizeof *image->pixels);
            image->mask = malloc(256);
        }
        if (!image || !image->pixels || !image->mask)
        {
            pmPointerImageFree(image);
            return NULL;
        }
        for (unsigned i = 0; i < 256; i++)
        {
            char c = pointer_shapes[s].rows[i / 16][i % 16];
            image->mask[i] = c == ' ';
            image->pixels[i] = c == '.' ? 0xffffff : 0;
        }
        p->hot_x = pointer_shapes[s].hot_x;
        p->hot_y = pointer_shapes[s].hot_y;
        return p->shape = image;
    }
    return NULL;
}

BOOL WinQueryPointerInfo(HPOINTER handle, LXPOINTERINFO *out)
{
    LxHandleBorrow b = {0};
    if (!out || !lx_handle_borrow(handle, LX_HANDLE_PM_POINTER, &b))
        return FALSE;
    PmPointer *p = b.resource;
    PmPointerImage *image = p->image ? p->image : pointer_shape(p);
    BOOL ok = image != NULL;
    if (ok && !p->info_mask)
    {
        p->info_mask = lx_pm_bitmap_create(image->width, image->height * 2, 1, 1);
        p->info_color = lx_pm_bitmap_create(image->width, image->height, 1, 24);
        LxHandleBorrow mb = {0}, cb = {0};
        PmBitmap *m = lx_pm_bitmap(p->info_mask, &mb), *c = lx_pm_bitmap(p->info_color, &cb);
        ok = m && c;
        for (unsigned y = 0; ok && y < image->height; y++)
            for (unsigned x = 0; x < image->width; x++)
            {
                size_t src = (size_t)(image->height - 1 - y) * image->width + x,
                       dest = (size_t)y * image->width + x;
                m->pixels[dest] = image->mask[src] ? 0xffffff : 0;
                m->pixels[dest + (size_t)image->height * image->width] = 0;
                c->pixels[dest] = image->pixels[src];
            }
        lx_handle_release(&cb);
        lx_handle_release(&mb);
        if (!ok)
        {
            if (p->info_mask)
                lx_pm_bitmap_delete(p->info_mask);
            if (p->info_color)
                lx_pm_bitmap_delete(p->info_color);
            p->info_mask = p->info_color = 0;
        }
    }
    if (ok)
        *out = (LXPOINTERINFO){1,
                               (LONG)p->hot_x,
                               (LONG)image->height - 1 - (LONG)p->hot_y,
                               p->info_mask,
                               p->info_color,
                               0,
                               0};
    lx_handle_release(&b);
    return ok;
}

HPOINTER lx_pm_pointer_file(const char *filename)
{
    AnchorBlock *a = getAnchorBlockNoHAB();
    if (!a || !filename || !GLoaderState.makeUnixPath)
        return 0;
    uint32 error = 0;
    char *path = GLoaderState.makeUnixPath(filename, &error);
    if (!path)
        return 0;
    LxHostFsError status;
    size_t size = 0;
    uint8 *bytes = lx_host_file_snapshot(path, 64 * 1024 * 1024, &size, &status);
    free(path);
    if (!bytes)
        return 0;
    PmPointerImage *image = pmPointerImage(bytes, size);
    if (!image)
        image = tb_icon_executable(bytes, size);
    free(bytes);
    if (!image)
        return 0;
    PmPointer *p = calloc(1, sizeof *p);
    if (!p)
    {
        pmPointerImageFree(image);
        return 0;
    }
    p->anchor = a;
    p->image = image;
    p->copy = TRUE;
    p->handle = lx_handle_create(LX_HANDLE_PM_POINTER, p, release_pointer);
    if (!p->handle)
    {
        free(p);
        pmPointerImageFree(image);
        return 0;
    }
    p->next = a->pointers;
    a->pointers = p;
    return p->handle;
}
