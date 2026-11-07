/* Public domain. No rights reserved. Wholly AI-authored. */
/* Logical palettes retain RGB entries; the display itself is true color. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmgpi.h"
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
    if (!input || lx_guest_load_u16(input) != 0x300 || !lx_guest_load_u16((uint8 *)input + 2))
    {
        open32SetError(87);
        return 0;
    }
    ULONG count = lx_guest_load_u16((uint8 *)input + 2);
    Open32Palette *palette = calloc(1, sizeof *palette);
    if (palette)
        palette->entries = calloc(count, 4);
    if (!palette || !palette->entries)
    {
        if (palette)
            free_palette(palette);
        open32SetError(8);
        return 0;
    }
    palette->count = count;
    memcpy(palette->entries, (uint8 *)input + 4, count * 4);
    ULONG handle = lx_handle_create(LX_HANDLE_OPEN32_PALETTE, palette, free_palette);
    if (!handle)
    {
        free_palette(palette);
        open32SetError(8);
    }
    return handle;
}
ULONG open32StockPalette(void)
{
    static _Thread_local ULONG stock;
    if (!stock)
    {
        /* The twenty system palette entries, in PALETTEENTRY RGB order. */
        static const uint8 colors[20][4] = {
            {0, 0, 0, 0},       {128, 0, 0, 0},     {0, 128, 0, 0},     {128, 128, 0, 0},
            {0, 0, 128, 0},     {128, 0, 128, 0},   {0, 128, 128, 0},   {192, 192, 192, 0},
            {192, 220, 192, 0}, {166, 202, 240, 0}, {255, 251, 240, 0}, {160, 160, 164, 0},
            {128, 128, 128, 0}, {255, 0, 0, 0},     {0, 255, 0, 0},     {255, 255, 0, 0},
            {0, 0, 255, 0},     {255, 0, 255, 0},   {0, 255, 255, 0},   {255, 255, 255, 0}};
        uint8 wire[84];
        lx_guest_store_u16(wire, 0x300);
        lx_guest_store_u16(wire + 2, 20);
        memcpy(wire + 4, colors, sizeof colors);
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
        open32SetError(6);
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
        open32SetError(6);
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
        open32SetError(6);
        return 0;
    }
    Open32Palette *palette = borrow.resource;
    ULONG copied = !output                          ? palette->count
                   : start >= palette->count        ? 0
                   : count < palette->count - start ? count
                                                    : palette->count - start;
    if (output && copied)
        memcpy(output, palette->entries + 4 * start, copied * 4);
    lx_handle_release(&borrow);
    return copied;
}
ULONG GetNearestPaletteIndex(ULONG handle, ULONG color)
{
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_PALETTE, &borrow))
    {
        open32SetError(6);
        return UINT32_MAX;
    }
    Open32Palette *palette = borrow.resource;
    ULONG nearest = 0, distance = UINT32_MAX;
    for (ULONG i = 0; i < palette->count; i++)
    {
        LONG r = (LONG)(color & 255) - palette->entries[i * 4],
             g = (LONG)((color >> 8) & 255) - palette->entries[i * 4 + 1],
             b = (LONG)((color >> 16) & 255) - palette->entries[i * 4 + 2];
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
    LONG result = !output || capacity >= 2 ? 2 : 0;
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
        open32SetError(87);
        return 0;
    }
    if (!count)
        return 1;
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_PALETTE, &borrow))
    {
        open32SetError(6);
        return 0;
    }
    Open32Palette *palette = borrow.resource;
    ULONG copied = first < palette->count ? palette->count - first : 0;
    if (copied > count)
        copied = count;
    if (copied)
        memcpy(palette->entries + first * 4, input, copied * 4);
    lx_handle_release(&borrow);
    open32MetafilePaletteChange(handle, 0x37, first, copied, input);
    return copied;
}
BOOL AnimatePalette(ULONG handle, ULONG first, ULONG count, PVOID input)
{
    LxHandleBorrow borrow = {0};
    if (!input)
    {
        open32SetError(87);
        return FALSE;
    }
    if (!count)
        return TRUE;
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_PALETTE, &borrow))
    {
        open32SetError(6);
        return FALSE;
    }
    Open32Palette *palette = borrow.resource;
    /* A range past the end is clipped; a start past the end is invalid. */
    BOOL valid = first <= palette->count;
    if (valid && count > palette->count - first)
        count = palette->count - first;
    if (valid && count)
        memcpy(palette->entries + first * 4, input, (size_t)count * 4);
    lx_handle_release(&borrow);
    if (!valid)
        open32SetError(87);
    else
        open32MetafilePaletteChange(handle, 0x436, first, count, input);
    return valid;
}
BOOL ResizePalette(ULONG handle, ULONG count)
{
    LxHandleBorrow borrow = {0};
    if (!count || count > 65535 || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_PALETTE, &borrow))
    {
        open32SetError(!count || count > 65535 ? 87 : 6);
        return FALSE;
    }
    Open32Palette *palette = borrow.resource;
    BOOL valid = !palette->stock;
    if (valid)
    {
        uint8 *entries = realloc(palette->entries, (size_t)count * 4);
        if (!entries)
        {
            lx_handle_release(&borrow);
            open32SetError(8);
            return FALSE;
        }
        palette->entries = entries;
    }
    if (valid)
    {
        if (count > palette->count)
            memset(palette->entries + palette->count * 4, 0, (count - palette->count) * 4);
        palette->count = count;
    }
    lx_handle_release(&borrow);
    if (!valid)
        open32SetError(87);
    else
        open32MetafilePaletteChange(handle, 0x139, count, 0, NULL);
    return valid;
}
/* The device's real color table comes from GPI, in PALETTEENTRY byte order. */
ULONG GetSystemPaletteEntries(ULONG dc, ULONG first, ULONG count, PVOID output)
{
    if (!open32FindDC(dc))
    {
        open32SetError(6);
        return 0;
    }
    LONG colors[256];
    if (first > 255)
        return 0;
    LONG available = GpiQueryRealColors(dc, 0, (LONG)first, 256, colors);
    if (available < 0)
        return 0;
    if (!output)
        return (ULONG)available + first;
    ULONG copied = count < (ULONG)available ? count : (ULONG)available;
    for (ULONG i = 0; i < copied; i++)
    {
        uint8 *entry = (uint8 *)output + i * 4;
        ULONG rgb = (ULONG)colors[i];
        entry[0] = (uint8)(rgb >> 16);
        entry[1] = (uint8)(rgb >> 8);
        entry[2] = (uint8)rgb;
        entry[3] = 0;
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
