/* Public domain. No rights reserved. Wholly AI-authored. */
/* Logical palettes retain RGB entries; the display itself is true color. */
#include "os2/ps/awe/gdi/dc.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/pmgpi.h"
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"

enum
{
    /* LOGPALETTE counts its entries in a WORD. */
    PALETTE_ENTRIES_MAX = 65535,
    STOCK_PALETTE_ENTRIES = 20,
    /* The device colour table GPI reports, and the most colours an indexed bitmap has. */
    SYSTEM_PALETTE_ENTRIES = 256,
    INDEXED_DEPTH_MAX = 8,
    INDEXED_COLORS_MAX = 256,
    /* A COLORREF's high byte: 0 for RGB, 1 for PALETTEINDEX, 2 for PALETTERGB. */
    COLORREF_KIND_SHIFT = 24,
    COLORREF_PALETTE_INDEX = 1,
    COLORREF_PALETTE_RGB = 2,
    COLORREF_RGB_MASK = 0xFFFFFF,
    /* PALETTEINDEX keeps the index in the low word; bits 16-23 must be clear. */
    PALETTE_INDEX_MASK = 0xFFFF,
    PALETTE_INDEX_RESERVED_MASK = 0xFF0000
};

typedef struct Open32Palette
{
    ULONG count;
    BOOL stock;
    uint8 *entries;
} Open32Palette;

static void free_palette(void *resource)
{
    Open32Palette *palette = resource;
    free(palette->entries);
    free(palette);
}

ULONG CreatePalette(PVOID input)
{
    if (!input ||
        lx_guest_load_u16((uint8 *)input + WIN32_LOGPALETTE_VERSION) != WIN32_PALETTE_VERSION ||
        !lx_guest_load_u16((uint8 *)input + WIN32_LOGPALETTE_ENTRY_COUNT))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ULONG count = lx_guest_load_u16((uint8 *)input + WIN32_LOGPALETTE_ENTRY_COUNT);
    Open32Palette *palette = calloc(1, sizeof *palette);
    if (palette)
        palette->entries = calloc(count, WIN32_PALETTEENTRY_SIZE);
    if (!palette || !palette->entries)
    {
        if (palette)
            free_palette(palette);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    palette->count = count;
    memcpy(palette->entries, (uint8 *)input + WIN32_LOGPALETTE_ENTRIES,
           count * WIN32_PALETTEENTRY_SIZE);
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_PALETTE, palette, free_palette);
    if (!handle)
    {
        free_palette(palette);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    }
    return handle;
}

ULONG open32StockPalette(void)
{
    static _Thread_local ULONG stock;
    if (!stock)
    {
        /* The twenty system palette entries, in PALETTEENTRY RGB order. */
        static const uint8 colors[STOCK_PALETTE_ENTRIES][WIN32_PALETTEENTRY_SIZE] = {
            {0, 0, 0, 0},       {128, 0, 0, 0},     {0, 128, 0, 0},     {128, 128, 0, 0},
            {0, 0, 128, 0},     {128, 0, 128, 0},   {0, 128, 128, 0},   {192, 192, 192, 0},
            {192, 220, 192, 0}, {166, 202, 240, 0}, {255, 251, 240, 0}, {160, 160, 164, 0},
            {128, 128, 128, 0}, {255, 0, 0, 0},     {0, 255, 0, 0},     {255, 255, 0, 0},
            {0, 0, 255, 0},     {255, 0, 255, 0},   {0, 255, 255, 0},   {255, 255, 255, 0}};
        uint8 wire[WIN32_LOGPALETTE_ENTRIES + sizeof colors];
        lx_guest_store_u16(wire + WIN32_LOGPALETTE_VERSION, WIN32_PALETTE_VERSION);
        lx_guest_store_u16(wire + WIN32_LOGPALETTE_ENTRY_COUNT, STOCK_PALETTE_ENTRIES);
        memcpy(wire + WIN32_LOGPALETTE_ENTRIES, colors, sizeof colors);
        ULONG handle = CreatePalette(wire);
        LxHandleBorrow borrow = {0};
        if (!handle || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_PALETTE, &borrow))
            return 0;
        ((Open32Palette *)borrow.resource)->stock = TRUE;
        lx_handle_release(&borrow);
        stock = handle;
    }
    return stock;
}

ULONG SelectPalette(ULONG handle, ULONG palette, BOOL background)
{
    (void)background;
    Open32DC *dc = open32FindDC(handle);
    LxHandleBorrow next = {0};
    if (!dc || !lx_handle_borrow(palette, LX_HANDLE_OPEN32_PALETTE, &next))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    ULONG previous = dc->palette_handle;
    lx_handle_release(&dc->palette);
    dc->palette = next;
    dc->palette_handle = palette;
    open32MetafilePalette(handle, palette);
    return previous;
}

ULONG RealizePalette(ULONG handle)
{
    if (!open32FindDC(handle))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return UINT32_MAX;
    }
    open32MetafileRealize(handle);
    /* A true-color drawable has no hardware palette entries to remap. */
    return 0;
}

ULONG GetPaletteEntries(ULONG handle, ULONG start, ULONG count, PVOID output)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_PALETTE, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    Open32Palette *palette = borrow.resource;
    ULONG copied = !output                          ? palette->count
                   : start >= palette->count        ? 0
                   : count < palette->count - start ? count
                                                    : palette->count - start;
    if (output && copied)
        memcpy(output, palette->entries + WIN32_PALETTEENTRY_SIZE * start,
               copied * WIN32_PALETTEENTRY_SIZE);
    lx_handle_release(&borrow);
    return copied;
}

ULONG GetNearestPaletteIndex(ULONG handle, ULONG color)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_PALETTE, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return UINT32_MAX;
    }
    Open32Palette *palette = borrow.resource;
    ULONG nearest = 0, distance = UINT32_MAX;
    for (ULONG i = 0; i < palette->count; i++)
    {
        const uint8 *entry = palette->entries + i * WIN32_PALETTEENTRY_SIZE;
        LONG r = (LONG)(color & 255) - entry[WIN32_PALETTEENTRY_RED],
             g = (LONG)((color >> 8) & 255) - entry[WIN32_PALETTEENTRY_GREEN],
             b = (LONG)((color >> 16) & 255) - entry[WIN32_PALETTEENTRY_BLUE];
        ULONG next = (ULONG)(r * r + g * g + b * b);
        if (next < distance)
        {
            distance = next;
            nearest = i;
        }
    }
    lx_handle_release(&borrow);
    return nearest;
}

BOOL open32DeletePalette(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_PALETTE, &borrow))
        return FALSE;
    Open32Palette *palette = borrow.resource;
    BOOL result =
        !palette->stock && lx_handle_close_guarded(handle, LX_HANDLE_OPEN32_PALETTE, 1, NULL) == 1;
    lx_handle_release(&borrow);
    return result;
}

LONG open32PaletteObject(ULONG handle, LONG capacity, PVOID output)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_PALETTE, &borrow))
        return 0;
    /* A palette's GetObject is its WORD entry count. */
    LONG result = !output || capacity >= (LONG)sizeof(uint16) ? (LONG)sizeof(uint16) : 0;
    if (output && result)
        lx_guest_store_u16(output, (uint16)((Open32Palette *)borrow.resource)->count);
    lx_handle_release(&borrow);
    return result;
}

/* Open32 animates only PC_RESERVED entries; ordinary writes clear explicit-index flags. */
ULONG SetPaletteEntries(ULONG handle, ULONG first, ULONG count, PVOID input)
{
    LxHandleBorrow borrow = {0};
    if (!input)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!count)
        return 1;
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_PALETTE, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    Open32Palette *palette = borrow.resource;
    ULONG copied = first < palette->count ? palette->count - first : 0;
    if (copied > count)
        copied = count;
    if (copied)
        memcpy(palette->entries + first * WIN32_PALETTEENTRY_SIZE, input,
               copied * WIN32_PALETTEENTRY_SIZE);
    lx_handle_release(&borrow);
    open32MetafilePaletteChange(handle, WIN32_META_SETPALENTRIES, first, copied, input);
    return copied;
}

BOOL AnimatePalette(ULONG handle, ULONG first, ULONG count, PVOID input)
{
    LxHandleBorrow borrow = {0};
    if (!input)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!count)
        return TRUE;
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_PALETTE, &borrow))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    Open32Palette *palette = borrow.resource;
    /* A range past the end is clipped; a start past the end is invalid. */
    BOOL valid = first <= palette->count;
    if (valid && count > palette->count - first)
        count = palette->count - first;
    if (valid && count)
        memcpy(palette->entries + first * WIN32_PALETTEENTRY_SIZE, input,
               (size_t)count * WIN32_PALETTEENTRY_SIZE);
    lx_handle_release(&borrow);
    if (!valid)
        open32SetError(ERROR_INVALID_PARAMETER);
    else
        open32MetafilePaletteChange(handle, WIN32_META_ANIMATEPALETTE, first, count, input);
    return valid;
}

BOOL ResizePalette(ULONG handle, ULONG count)
{
    LxHandleBorrow borrow = {0};
    if (!count || count > PALETTE_ENTRIES_MAX ||
        !lx_handle_borrow(handle, LX_HANDLE_OPEN32_PALETTE, &borrow))
    {
        open32SetError(!count || count > PALETTE_ENTRIES_MAX ? ERROR_INVALID_PARAMETER
                                                             : ERROR_INVALID_HANDLE);
        return FALSE;
    }
    Open32Palette *palette = borrow.resource;
    BOOL valid = !palette->stock;
    if (valid)
    {
        uint8 *entries = realloc(palette->entries, (size_t)count * WIN32_PALETTEENTRY_SIZE);
        if (!entries)
        {
            lx_handle_release(&borrow);
            open32SetError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        palette->entries = entries;
    }
    if (valid)
    {
        if (count > palette->count)
            memset(palette->entries + palette->count * WIN32_PALETTEENTRY_SIZE, 0,
                   (count - palette->count) * WIN32_PALETTEENTRY_SIZE);
        palette->count = count;
    }
    lx_handle_release(&borrow);
    if (!valid)
        open32SetError(ERROR_INVALID_PARAMETER);
    else
        open32MetafilePaletteChange(handle, WIN32_META_RESIZEPALETTE, count, 0, NULL);
    return valid;
}

/* The device's real color table comes from GPI, in PALETTEENTRY byte order. */
ULONG GetSystemPaletteEntries(ULONG dc, ULONG first, ULONG count, PVOID output)
{
    if (!open32FindDC(dc))
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return 0;
    }
    LONG colors[SYSTEM_PALETTE_ENTRIES];
    if (first > SYSTEM_PALETTE_ENTRIES - 1)
        return 0;
    LONG available = GpiQueryRealColors(dc, 0, (LONG)first, SYSTEM_PALETTE_ENTRIES, colors);
    if (available < 0)
        return 0;
    if (!output)
        return (ULONG)available + first;
    ULONG copied = count < (ULONG)available ? count : (ULONG)available;
    for (ULONG i = 0; i < copied; i++)
    {
        uint8 *entry = (uint8 *)output + i * WIN32_PALETTEENTRY_SIZE;
        ULONG rgb = (ULONG)colors[i];
        entry[WIN32_PALETTEENTRY_RED] = (uint8)(rgb >> 16);
        entry[WIN32_PALETTEENTRY_GREEN] = (uint8)(rgb >> 8);
        entry[WIN32_PALETTEENTRY_BLUE] = (uint8)rgb;
        entry[WIN32_PALETTEENTRY_FLAGS] = 0;
    }
    return copied;
}

BOOL UnrealizeObject(ULONG object)
{
    /* Logical objects are applied directly on this true-color backend, so there is no
       device realization to discard. PMWINX reports success for every handle. */
    (void)object;
    return TRUE;
}

ULONG GetNearestColor(ULONG handle, ULONG color)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc)
    {
        open32SetError(ERROR_INVALID_HANDLE);
        return UINT32_MAX;
    }
    ULONG kind = color >> COLORREF_KIND_SHIFT;
    if (kind == COLORREF_PALETTE_INDEX)
    {
        UCHAR entry[WIN32_PALETTEENTRY_SIZE];
        if ((color & PALETTE_INDEX_RESERVED_MASK) ||
            GetPaletteEntries(dc->palette_handle, color & PALETTE_INDEX_MASK, 1, entry) != 1)
        {
            open32SetError(ERROR_INVALID_PARAMETER);
            return UINT32_MAX;
        }
        color = (ULONG)entry[WIN32_PALETTEENTRY_RED] | (ULONG)entry[WIN32_PALETTEENTRY_GREEN] << 8 |
                (ULONG)entry[WIN32_PALETTEENTRY_BLUE] << 16;
    }
    else if (kind && kind != COLORREF_PALETTE_RGB)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return UINT32_MAX;
    }
    color &= COLORREF_RGB_MASK;
    PresentationSpace *ps = lx_pm_font_space(handle);
    PmBitmap *bitmap = ps ? ps->bitmap_lease.resource : NULL;
    if (!bitmap || bitmap->depth > INDEXED_DEPTH_MAX)
        return color;
    uint32 colors[INDEXED_COLORS_MAX];
    open32BitmapPalette(bitmap->depth, colors);
    unsigned count = bitmap->palette_count ? bitmap->palette_count : 1u << bitmap->depth;
    if (bitmap->palette_count)
        memcpy(colors, bitmap->palette, count * sizeof *colors);
    ULONG distance = UINT32_MAX, best = 0;
    for (unsigned i = 0; i < count; i++)
    {
        LONG r = (LONG)(color & 255) - (LONG)((colors[i] >> 16) & 255);
        LONG g = (LONG)((color >> 8) & 255) - (LONG)((colors[i] >> 8) & 255);
        LONG b = (LONG)((color >> 16) & 255) - (LONG)(colors[i] & 255);
        ULONG next = (ULONG)(r * r + g * g + b * b);
        if (next < distance)
        {
            distance = next;
            best = colors[i];
        }
    }
    return (best & 255) << 16 | (best & 0xFF00) | (best >> 16 & 255);
}
