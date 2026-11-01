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
            result = open32BrushObject(handle, capacity, output);
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
    if (lx_pm_bitmap_delete(handle) || open32DeleteFont(handle) || open32DeleteBrush(handle))
        return TRUE;
    open32SetError(6);
    return FALSE;
}
