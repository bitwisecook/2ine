/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 device-dependent bitmaps share PM's checked bitmap objects. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmgpi.h"
#include "os2libs/pmwinx/drawing.h"
ULONG LoadBitmap(HMODULE module, PSZ name)
{
    TRACE_NATIVE("LoadBitmap(%u, %p)", module, name);
    ULONG resource = FindResource(module, name, (PSZ)(uintptr_t)2);
    if (!resource)
        return 0;
    if ((!module || module == UINT32_MAX) && GLoaderState.main_module)
        module = GLoaderState.main_module->handle;
    if (!open32Presentation())
    {
        open32SetError(8);
        return 0;
    }
    HPS ps = WinGetPS(HWND_DESKTOP);
    ULONG bitmap = ps ? GpiLoadBitmap(ps, module, resource & 0xFFFF, 0, 0) : 0;
    if (ps)
        WinReleasePS(ps);
    if (!bitmap)
        open32SetError(1814);
    return bitmap;
}
LONG GetObject(ULONG handle, LONG capacity, PVOID output)
{
    if (capacity < 0)
    {
        open32SetError(87);
        return 0;
    }
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = lx_pm_bitmap(handle, &borrow);
    if (!bitmap)
    {
        LONG result = open32FontObject(handle, capacity, output);
        if (!result)
            result = open32PenObject(handle, capacity, output);
        if (!result)
            result = open32BrushObject(handle, capacity, output);
        if (!result)
            result = open32PaletteObject(handle, capacity, output);
        if (!result)
            open32SetError(6);
        return result;
    }
    if (!bitmap || capacity < 0 || (output && capacity < 24))
    {
        lx_handle_release(&borrow);
        open32SetError(bitmap ? 87 : 6);
        return 0;
    }
    if (output)
    {
        uint8 bytes[24] = {0};
        lx_guest_store_u32(bytes + 4, bitmap->width);
        lx_guest_store_u32(bytes + 8, bitmap->height);
        lx_guest_store_u32(bytes + 12, ((bitmap->width * bitmap->depth + 15) / 16) * 2);
        lx_guest_store_u16(bytes + 16, bitmap->planes);
        lx_guest_store_u16(bytes + 18, bitmap->depth);
        memcpy(output, bytes, sizeof bytes);
    }
    lx_handle_release(&borrow);
    return 24;
}
BOOL DeleteObject(ULONG handle)
{
    if (lx_pm_bitmap_delete(handle) || open32DeleteFont(handle) || open32DeleteBrush(handle) ||
        open32DeletePalette(handle) || open32DeleteRegion(handle) || open32DeletePen(handle))
        return TRUE;
    open32SetError(6);
    return FALSE;
}

BOOL open32BitmapBytes(LONG width, LONG height, ULONG planes, ULONG depth, size_t *bytes)
{
    if (width < 1 || height < 1 || width > 16384 || height > 16384 ||
        (uint64_t)width * height > 16777216)
    {
        open32SetError(87);
        return FALSE;
    }
    if (planes != 1 || (depth != 1 && depth != 4 && depth != 8 && depth != 24 && depth != 32))
    {
        open32SetError(50);
        return FALSE;
    }
    *bytes = ((size_t)width * depth + 15) / 16 * 2 * (size_t)height;
    return TRUE;
}
void open32BitmapPalette(ULONG depth, uint32 colors[256])
{
    static const uint32 vga[16] = {0,        0x800000, 0x008000, 0x808000, 0x000080, 0x800080,
                                   0x008080, 0xC0C0C0, 0x808080, 0xFF0000, 0x00FF00, 0xFFFF00,
                                   0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF};
    memset(colors, 0, 256 * sizeof *colors);
    unsigned count = depth <= 8 ? 1u << depth : 0;
    for (unsigned i = 0; i < count; i++)
        colors[i] = depth == 1   ? (i ? 0xFFFFFF : 0)
                    : depth == 4 ? vga[i]
                                 : ((i >> 5) * 255 / 7) << 16 | (((i >> 2) & 7) * 255 / 7) << 8 |
                                       (i & 3) * 85;
}
ULONG CreateBitmap(LONG width, LONG height, ULONG planes, ULONG depth, PVOID bits)
{
    TRACE_NATIVE("CreateBitmap(%d, %d, %u, %u, %p)", width, height, planes, depth, bits);
    size_t bytes;
    if (!open32BitmapBytes(width, height, planes, depth, &bytes) || !open32Presentation())
        return 0;
    ULONG handle = lx_pm_bitmap_create((unsigned)width, (unsigned)height, planes, depth);
    LxHandleBorrow borrow = {0};
    PmBitmap *bitmap = handle ? lx_pm_bitmap(handle, &borrow) : NULL;
    if (!bitmap)
    {
        open32SetError(8);
        return 0;
    }
    if (bits)
    {
        size_t stride = bytes / (size_t)height;
        if (depth <= 8)
        {
            bitmap->palette_count = 1u << depth;
            open32BitmapPalette(depth, bitmap->palette);
        }
        for (unsigned y = 0; y < (unsigned)height; y++)
        {
            const uint8 *row = (const uint8 *)bits + y * stride;
            for (unsigned x = 0; x < (unsigned)width; x++)
            {
                uint32 rgb;
                if (depth <= 8)
                {
                    unsigned bit = x * depth,
                             index = (row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
                    rgb = bitmap->palette[index];
                }
                else
                {
                    const uint8 *pixel = row + x * (depth / 8);
                    rgb = (uint32)pixel[2] << 16 | (uint32)pixel[1] << 8 | pixel[0];
                }
                bitmap->pixels[(size_t)y * width + x] = rgb;
            }
        }
    }
    lx_handle_release(&borrow);
    return handle;
}
