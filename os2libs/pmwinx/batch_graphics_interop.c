/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2libs/pmwinx/drawing.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwin/graphics.h"
#include "os2libs/pmwin/internal.h"
#include "os2libs/pmgpi.h"
#include "os2libs/printer_catalogue.h"
LONG SetAbortProc(ULONG h, ULONG procedure)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || !procedure || !open32ValidProcedure(procedure))
    {
        open32SetError(87);
        return -1;
    }
    dc->abort_proc = procedure;
    return 1;
}
BOOL open32AbortCheck(ULONG h)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc)
        return FALSE;
    if (!dc->abort_proc)
        return TRUE;
    return lx_execution_callback32_4(GLoaderState.execution, dc->abort_proc, h, 0, 0, 0) != 0;
}
BOOL ResetDC(ULONG h, PVOID mode)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || (dc->kind != 5 && dc->kind != 6) || dc->print_page || !mode)
    {
        open32SetError(87);
        return 0;
    }
    unsigned kind = lx_pm_printer_kind(GpiQueryDevice(h));
    if (!open32PrinterDefaults(kind, NULL, mode))
        return 0;
    /* Virtual queues expose a single A4/300dpi mode. Applying that mode keeps
       the HDC identity, as confirmed by the original's return instruction. */
    return h;
}
ULONG DeviceCapabilities(PSZ device, PSZ port, ULONG capability, PVOID output, PVOID mode)
{
    (void)port;
    (void)mode;
    int printer = lx_printer_index(device);
    if (printer < 0)
    {
        open32SetError(1801);
        return UINT32_MAX;
    }
    switch (capability)
    {
        case 1:
            return 0x2c03; /* DEVMODE fields */
        case 2:
            if (output)
                lx_guest_store_u16(output, 9);
            return 1;
        case 3:
            if (output)
            {
                lx_guest_store_u32(output, 2100);
                lx_guest_store_u32((uint8 *)output + 4, 2970);
            }
            return 1;
        case 4:
            return 2100u | (2970u << 16);
        case 5:
            return 2100u | (2970u << 16);
        case 6:
            if (output)
                lx_guest_store_u16(output, 7);
            return 1;
        case 7:
            return 0;
        case 8:
            return 128;
        case 9:
            return 0;
        case 10:
            return 0x320;
        case 11:
            return 1;
        case 12:
            if (output)
            {
                memset(output, 0, 24);
                memcpy(output, "Auto", 5);
            }
            return 1;
        case 13:
            if (output)
            {
                lx_guest_store_u32(output, 300);
                lx_guest_store_u32((uint8 *)output + 4, 300);
            }
            return 1;
        case 14:
            return 0;
        case 15:
            return 0;
        case 16:
            if (output)
            {
                memset(output, 0, 64);
                memcpy(output, "A4", 3);
            }
            return 1;
        case 17:
            return 0;
        case 18:
            return 1;
        case 19:
            return 0;
        case 20:
            return 0;
        case 22:
            return printer == 0;
        case 32:
            return printer == 0 ? 24 : 1;
        default:
            return UINT32_MAX;
    }
}
LONG Escape(ULONG h, LONG code, LONG count, PVOID input, PVOID output)
{
    Open32DC *dc = open32FindDC(h);
    if (!dc || count < 0 || (count && !input))
    {
        open32SetError(87);
        return 0;
    }
    if (code == 8)
    {
        if (count < 2)
            return 0;
        ULONG query = count >= 4 ? lx_guest_load_u32(input) : lx_guest_load_u16(input);
        return query == 1 || query == 2 || query == 8 || query == 9 || query == 10 || query == 11 ||
               query == 12 || query == 13 || query == 14;
    }
    if (code == 1)
    {
        LONG result = EndPage(h);
        if (result > 0)
            StartPage(h);
        return result;
    }
    if (code == 2)
        return AbortDoc(h);
    if (code == 10)
    {
        if (!input || !count)
            return 0;
        ULONG address = 0;
        LxGuestMapping *title =
            lx_execution_map(GLoaderState.execution, 0, (size_t)count + 1, 3, 0);
        if (!title)
        {
            open32SetError(8);
            return 0;
        }
        memcpy(lx_execution_mapping_host(title), input, (size_t)count);
        ((char *)lx_execution_mapping_host(title))[count] = 0;
        address = lx_execution_mapping_address(title);
        uint8 info[12] = {0};
        lx_guest_store_u32(info, 12);
        lx_guest_store_u32(info + 4, address);
        LONG result = StartDoc(h, info);
        lx_execution_unmap(GLoaderState.execution, title);
        if (result > 0)
            StartPage(h);
        return result;
    }
    if (code == 11)
        return EndDoc(h);
    if (code == 12 || code == 13 || code == 14)
    {
        if (!output)
            return 0;
        ULONG x = code == 12   ? (ULONG)GetDeviceCaps(h, 110)
                  : code == 14 ? 1
                               : 0,
              y = code == 12   ? (ULONG)GetDeviceCaps(h, 111)
                  : code == 14 ? 1
                               : 0;
        lx_guest_store_u32(output, x);
        lx_guest_store_u32((uint8 *)output + 4, y);
        return 1;
    }
    if (output)
    {
        open32SetError(50);
        return 0;
    }
    return DevEscape(GpiQueryDevice(h), code, count, input, NULL, NULL);
}
BOOL WinTranslateDeviceRects(ULONG h, ULONG window, PVOID rectangles, LONG count, ULONG direction)
{
    (void)direction;
    if (!h && !window)
        return FALSE;
    if (count < 0 || count > 65536 || (count && !rectangles))
    {
        open32SetError(87);
        return FALSE;
    }
    LONG height;
    if (h)
    {
        if (!open32FindDC(h))
        {
            open32SetError(6);
            return FALSE;
        }
        height = open32DCHeight(h);
    }
    else
    {
        RECTL r;
        if (!WinQueryWindowRect(open32ClientWindow(window), &r))
        {
            open32SetError(1400);
            return FALSE;
        }
        height = r.yTop - r.yBottom;
    }
    for (LONG i = 0; i < count; i++)
    {
        uint8 *p = (uint8 *)rectangles + i * 16;
        LONG top = (LONG)lx_guest_load_u32(p + 4), bottom = (LONG)lx_guest_load_u32(p + 12);
        int64_t t = (int64_t)height - bottom, b = (int64_t)height - top;
        if (t < INT32_MIN || t > INT32_MAX || b < INT32_MIN || b > INT32_MAX)
        {
            open32SetError(87);
            return FALSE;
        }
        lx_guest_store_u32(p + 4, (ULONG)t);
        lx_guest_store_u32(p + 12, (ULONG)b);
    }
    return TRUE;
}
ULONG WinTranslateGraphicsObjectHandle(ULONG object, ULONG direction, ULONG type)
{
    direction &= 255;
    if (type != 5 && type != 7 && type != 8 && type != 9)
        return 0;
    if (type == 7)
    {
        LxHandleBorrow borrow = {0};
        BOOL ok = lx_pm_bitmap(object, &borrow) != NULL;
        lx_handle_release(&borrow);
        if (ok)
            return object;
    }
    if (type == 5)
    {
        if (direction == 0)
        {
            ULONG n = GetPaletteEntries(object, 0, 0, NULL);
            if (!n || n > 256)
            {
                open32SetError(87);
                return 0;
            }
            uint8 entries[1024];
            ULONG colors[256];
            if (GetPaletteEntries(object, 0, n, entries) != n)
                return 0;
            for (ULONG i = 0; i < n; i++)
                colors[i] = (ULONG)entries[i * 4] << 16 | (ULONG)entries[i * 4 + 1] << 8 |
                            entries[i * 4 + 2];
            return GpiCreatePalette(open32Presentation(), 0, 0, n, colors);
        }
        LONG n = GpiQueryPaletteInfo(object, 0, 0, 0, 0, NULL);
        if (n < 1 || n > 256)
        {
            open32SetError(6);
            return 0;
        }
        ULONG colors[256];
        if (GpiQueryPaletteInfo(object, 0, 0, 0, (ULONG)n, colors) != n)
            return 0;
        uint8 entries[1028];
        lx_guest_store_u16(entries, 0x300);
        lx_guest_store_u16(entries + 2, (uint16)n);
        for (LONG i = 0; i < n; i++)
        {
            entries[4 + i * 4] = (uint8)(colors[i] >> 16);
            entries[5 + i * 4] = (uint8)(colors[i] >> 8);
            entries[6 + i * 4] = (uint8)colors[i];
            entries[7 + i * 4] = 0;
        }
        return CreatePalette(entries);
    }
    if (type == 8)
    {
        LxHandleBorrow borrow = {0};
        LxHandleKind kind = direction ? LX_HANDLE_PM_REGION : LX_HANDLE_OPEN32_REGION;
        if (lx_handle_borrow(object, kind, &borrow))
        {
            PmRegion *r = borrow.resource;
            ULONG result = 0;
            if (direction)
            {
                result = CreateRectRgn(0, 0, 0, 0);
                LxHandleBorrow out = {0};
                if (result && lx_handle_borrow(result, LX_HANDLE_OPEN32_REGION, &out))
                {
                    if (!regionBoolean(out.resource, r, NULL, 2))
                    {
                        DeleteObject(result);
                        result = 0;
                    }
                    lx_handle_release(&out);
                }
            }
            else
            {
                ULONG ps = GetDC(0);
                if (ps)
                {
                    result = GpiCreateRegion(ps, (LONG)r->count, r->rects);
                    ReleaseDC(0, ps);
                }
            }
            lx_handle_release(&borrow);
            return result;
        }
    }
    /* PM and Open32 raster bitmaps already share their typed backing object.
       Native GPI metafile orders are a separate format from Windows WMF bytes. */
    if (type == 9 && GetObjectType(object) == 9)
        return object;
    open32SetError(6);
    return 0;
}
