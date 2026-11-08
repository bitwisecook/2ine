/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2/ps/awe/drawing.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/windows.h"
#include "os2/ps/pmwin/graphics.h"
#include "os2/ps/pmwin/internal.h"
#include "os2/include/pmgpi.h"
#include "os2/include/private/pmdevp.h"

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
    return lx_execution_callback32_4(GProcessState.execution, dc->abort_proc, h, 0, 0, 0) != 0;
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
    if (!open32PrinterDefaults(kind, NULL, mode) || !open32PrinterApply(h, mode))
        return 0;
    /* The original returns the HDC itself despite the SDK's BOOL declaration. */
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
    static const USHORT papers[3] = {1, 5, 9};
    static const ULONG sizes[3][2] = {{2159, 2794}, {2159, 3556}, {2100, 2970}};
    switch (capability)
    {
        case 1:
            return 0x2c03; /* DEVMODE fields */
        case 2:
            for (unsigned i = 0; output && i < 3; i++)
                lx_guest_store_u16((uint8 *)output + i * 2, papers[i]);
            return 3;
        case 3:
            for (unsigned i = 0; output && i < 3; i++)
            {
                lx_guest_store_u32((uint8 *)output + i * 8, sizes[i][0]);
                lx_guest_store_u32((uint8 *)output + i * 8 + 4, sizes[i][1]);
            }
            return 3;
        case 4:
            return 2100u | (2794u << 16);
        case 5:
            return 2159u | (3556u << 16);
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
                memset(output, 0, 192);
                memcpy(output, "Letter", 7);
                memcpy((uint8 *)output + 64, "Legal", 6);
                memcpy((uint8 *)output + 128, "A4", 3);
            }
            return 3;
        case 17:
            return 90;
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
               query == 12 || query == 13 || query == 14 || query == 7 || query == 17 ||
               query == 19 || query == 20 || query == 23 || query == 28 || query == 29 ||
               query == 30 || query == 31 || query == 37 || query == 0x101 || query == 0x200 ||
               query == 0x1000 || query == 0x1001 || query == 0x1002 || query == 0x1006;
    }
    if (code == 15)
        return 0;
    if (code == 7 || code == 17 || code == 28 || code == 30)
    {
        /* Job properties: a printer DC and a word, dword or 20-byte input. */
        if (!input)
            return 0;
        if (count != 2 && count != 4 && count != 20)
            return -1;
        if (dc->kind != 5 && dc->kind != 6)
            return 0;
        LONG value = count == 4 ? (LONG)lx_guest_load_u32(input) : (SHORT)lx_guest_load_u16(input);
        HDC device = GpiQueryDevice(h);
        if (code == 7)
            dc->print_draft = value != 0;
        else if (code == 17)
            dc->print_copies = value > 0 ? (ULONG)value : 1;
        else if (code == 28)
            dc->print_duplex = value == 0 ? 1 : value == 1 ? 2 : 3;
        else
        {
            unsigned width = 0, height = 0;
            BOOL landscape = lx_guest_load_u32(input) != 0;
            if (dc->print_page || !lx_pm_printer_size(device, &width, &height))
                return -1;
            if (landscape != (width > height) && !lx_pm_printer_page(device, height, width))
                return -1;
        }
        lx_pm_printer_options(device, dc->print_copies, dc->print_duplex, dc->print_draft);
        if (code == 17 && output)
        {
            if (count == 2)
                lx_guest_store_u16(output, (uint16)(dc->print_copies ? dc->print_copies : 1));
            else
                lx_guest_store_u32(output, dc->print_copies ? dc->print_copies : 1);
        }
        return 1;
    }
    if (code == 20)
    {
        /* The virtual queues and the display are raster devices. */
        if (!output)
            return 0;
        const char *technology =
            lx_pm_printer_kind(GpiQueryDevice(h)) ? "Raster Printer" : "Raster Display";
        memcpy(output, technology, strlen(technology) + 1);
        return 1;
    }
    /* SETLINECAP, SETLINEJOIN and GETFACENAME apply only to PostScript drivers. */
    if (code == 21 || code == 22 || code == 0x201)
        return 0;
    if (code == 23)
    {
        if (!input || !output || (count != 2 && count != 4))
            return 0;
        LONG previous = dc->miter_limit > 0 ? (LONG)dc->miter_limit : 10;
        LONG value = count == 4 ? (LONG)lx_guest_load_u32(input) : (SHORT)lx_guest_load_u16(input);
        if (value > 0)
            dc->miter_limit = value;
        if (count == 4)
            lx_guest_store_u32(output, (ULONG)previous);
        else
            lx_guest_store_u16(output, (uint16)previous);
        return 1;
    }
    if (code == 29)
    {
        /* The queues have one bin: the current bin and the bin count. */
        if (dc->kind != 5 && dc->kind != 6)
            return 0;
        if (output)
        {
            lx_guest_store_u16(output, 7);
            lx_guest_store_u16((uint8 *)output + 2, 1);
        }
        return 1;
    }
    if (code == 31)
    {
        if (dc->kind != 5 && dc->kind != 6)
            return 0;
        if (!input || (count != 2 && count != 4))
            return 0;
        LONG capacity =
            count == 4 ? (LONG)lx_guest_load_u32(input) : (SHORT)lx_guest_load_u16(input);
        if (output && capacity >= 1)
        {
            lx_guest_store_u16(output, 7);
            memset((uint8 *)output + (size_t)capacity * 2, 0, 24);
            memcpy((uint8 *)output + (size_t)capacity * 2, "Auto", 5);
        }
        return 1;
    }
    if (code == 0x101)
    {
        if (count != 2 || !output)
            return -1;
        ULONG first = ((uint8 *)input)[0], last = ((uint8 *)input)[1];
        if (last < first)
            return -1;
        uint8 widths[1024];
        if (!GetCharWidth(h, first, last, widths))
            return -1;
        for (ULONG i = 0; i <= last - first; i++)
            lx_guest_store_u16((uint8 *)output + i * 2, (uint16)lx_guest_load_u32(widths + i * 4));
        return 1;
    }
    if (code == 0x200)
    {
        if (count != 12 || !input)
            return 0;
        const uint8 *record = input;
        ULONG text = lx_guest_load_u32(record + 4), advances = lx_guest_load_u32(record + 8);
        char *string = text ? lx_execution_copy_string(GProcessState.execution, text, 65536) : NULL;
        if (!string)
            return 0;
        ULONG length = (ULONG)strlen(string);
        LxGuestBorrow widths = {0};
        BOOL ready = !advances || !length ||
                     lx_execution_borrow(GProcessState.execution, advances, (size_t)length * 4,
                                         LX_GUEST_READ, &widths);
        LONG result =
            ready && ExtTextOut(h, lx_guest_load_u16(record), lx_guest_load_u16(record + 2), 0,
                                NULL, string, length, (PLONG)widths.host);
        lx_execution_release(&widths);
        free(string);
        return result;
    }
    if (code == 0x1000)
        return BeginPath(h) ? 1 : -1;
    if (code == 0x1001)
        return open32PathClip(h, 5) ? 1 : -1;
    if (code == 0x1002)
        return EndPath(h) ? 1 : -1;
    if (code == 0x1006)
    {
        if (count < 2 || !input)
            return 0;
        LONG previous = dc->arc_clockwise ? 2 : 1;
        LONG value = count >= 4 ? (LONG)lx_guest_load_u32(input) : (SHORT)lx_guest_load_u16(input);
        if (value == 1 || value == 2)
            dc->arc_clockwise = value == 2;
        return previous;
    }
    if (code == 19 || code == 37)
    {
        /* PASSTHROUGH: a word count, then raw device data, sent as DEVESC_RAWDATA. */
        if (!input || count < 2)
            return 0;
        LONG size = lx_guest_load_u16(input);
        if (size > count - 2)
            return 0;
        return DevEscape(GpiQueryDevice(h), 0x3faf, size, (uint8 *)input + 2, NULL, NULL);
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
            lx_execution_map(GProcessState.execution, 0, (size_t)count + 1, 3, 0);
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
        lx_execution_unmap(GProcessState.execution, title);
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
        /* The virtual pages have no unprintable margin and no device scaling. */
        ULONG x = code == 12 ? (ULONG)GetDeviceCaps(h, 110) : 0,
              y = code == 12 ? (ULONG)GetDeviceCaps(h, 111) : 0;
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

/* Translating one object again yields the same partner while both handles stay valid. */
typedef struct TranslatedHandle
{
    ULONG open32, native, type;
    struct TranslatedHandle *next;
} TranslatedHandle;

static _Thread_local TranslatedHandle *translations;

static BOOL native_handle_valid(ULONG handle, ULONG type)
{
    LxHandleBorrow borrow = {0};
    BOOL valid = lx_handle_borrow(handle,
                                  type == 5   ? LX_HANDLE_PM_PALETTE
                                  : type == 8 ? LX_HANDLE_PM_REGION
                                              : LX_HANDLE_PM_METAFILE,
                                  &borrow);
    lx_handle_release(&borrow);
    return valid;
}

static ULONG translated_handle(ULONG object, ULONG direction, ULONG type)
{
    TranslatedHandle **link = &translations;
    while (*link)
    {
        TranslatedHandle *entry = *link;
        if (GetObjectType(entry->open32) != entry->type ||
            !native_handle_valid(entry->native, entry->type))
        {
            *link = entry->next;
            free(entry);
            continue;
        }
        if (entry->type == type && (direction ? entry->native : entry->open32) == object)
            return direction ? entry->open32 : entry->native;
        link = &entry->next;
    }
    return 0;
}

static ULONG remember_handle(ULONG open32, ULONG native, ULONG type, ULONG result)
{
    TranslatedHandle *entry = open32 && native ? malloc(sizeof *entry) : NULL;
    if (entry)
    {
        *entry = (TranslatedHandle){open32, native, type, translations};
        translations = entry;
    }
    return result;
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
    ULONG cached = translated_handle(object, direction, type);
    if (cached)
        return cached;
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
            ULONG native = GpiCreatePalette(open32Presentation(), 0, 0, n, colors);
            return remember_handle(object, native, type, native);
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
        ULONG palette = CreatePalette(entries);
        return remember_handle(palette, object, type, palette);
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
            return direction ? remember_handle(result, object, type, result)
                             : remember_handle(object, result, type, result);
        }
    }
    /* A native metafile object carries Windows metafile bytes across the two APIs; GPI order
       streams have no WMF reading, so they do not translate back. */
    if (type == 9 && direction)
    {
        LONG length = GpiQueryMetaFileLength(object);
        uint8 *bytes = length > 0 ? malloc((size_t)length) : NULL;
        ULONG result = 0;
        if (bytes && GpiQueryMetaFileBits(object, 0, length, bytes))
            result = SetMetaFileBitsEx((ULONG)length, bytes);
        free(bytes);
        if (result)
            return remember_handle(result, object, type, result);
    }
    else if (type == 9)
    {
        ULONG length = GetMetaFileBitsEx(object, 0, NULL);
        uint8 *bytes = length ? malloc(length) : NULL;
        ULONG result = 0;
        if (bytes && GetMetaFileBitsEx(object, length, bytes) == length)
            result = lx_pm_metafile_create(bytes, length);
        free(bytes);
        if (result)
            return remember_handle(object, result, type, result);
    }
    open32SetError(6);
    return 0;
}
