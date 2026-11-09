/* Public domain. No rights reserved. Wholly AI-authored. */
/* Printing: document and page brackets with the abort procedure, printer DC services
   (ResetDC, DeviceCapabilities, Escape) and job defaults from DEVMODE. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/dc.h"
#include "os2/include/private/pmgrep.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmdev.h"
#include "os2/include/private/pmsplp.h"
#include "host/print.h"

/* The abort procedure runs guest code at each page and document boundary, before any device
   output. It may itself end the job or delete the DC, so state is looked up again afterwards. */
enum
{
    SP_ERROR = -1,
    SP_APPABORT = -2
};

static LONG print_boundary(ULONG handle, Open32DC **context)
{
    BOOL proceed = open32AbortCheck(handle);
    Open32DC *dc = open32FindDC(handle);
    *context = dc;
    if (!dc)
        return SP_ERROR;
    if (proceed)
        return 1;
    if (dc->print_active)
        DevEscape(GpiQueryDevice(handle), 8153, 0, NULL, NULL, NULL);
    dc->print_active = dc->print_page = FALSE;
    return SP_APPABORT;
}

LONG StartDoc(ULONG handle, PVOID info)
{
    Open32DC *dc = open32FindDC(handle);
    ULONG size = info ? lx_guest_load_u32(info) : 0;
    if (!dc || dc->kind != 5 || dc->print_active || (size != 12 && size != 16 && size != 20))
    {
        open32SetError(87);
        return -1;
    }
    uint32 name = lx_guest_load_u32((uint8 *)info + 4),
           output = lx_guest_load_u32((uint8 *)info + 8);
    char *title = name ? lx_execution_copy_string(GProcessState.execution, name, 4096)
                       : strdup("Print Job"),
         *destination =
             output ? lx_execution_copy_string(GProcessState.execution, output, 4096) : NULL;
    if (!title || (output && !destination) ||
        (destination && *destination && strcasecmp(destination, "FILE:")))
    {
        free(title);
        free(destination);
        open32SetError(50);
        return -1;
    }
    LONG result = print_boundary(handle, &dc);
    if (result > 0)
    {
        if (dc->print_active)
            result = SP_ERROR;
        else
            result =
                DevEscape(GpiQueryDevice(handle), 8150, (LONG)strlen(title) + 1, title, NULL, NULL);
    }
    free(title);
    free(destination);
    if (result > 0)
    {
        dc->print_active = TRUE;
        dc->print_page = FALSE;
    }
    return result;
}

LONG StartPage(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !dc->print_active || dc->print_page)
        return SP_ERROR;
    LONG result = print_boundary(handle, &dc);
    if (result <= 0)
        return result;
    if (!dc->print_active || dc->print_page)
        return SP_ERROR;
    dc->print_page = TRUE;
    return 1;
}

LONG EndPage(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !dc->print_active || !dc->print_page)
        return SP_ERROR;
    LONG result = print_boundary(handle, &dc);
    if (result <= 0)
        return result;
    if (!dc->print_active || !dc->print_page)
        return SP_ERROR;
    result = DevEscape(GpiQueryDevice(handle), 16300, 0, NULL, NULL, NULL);
    if (result > 0)
        dc->print_page = FALSE;
    return result;
}

LONG EndDoc(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !dc->print_active)
        return SP_ERROR;
    LONG result = print_boundary(handle, &dc);
    if (result <= 0)
        return result;
    if (!dc->print_active)
        return SP_ERROR;
    result = DevEscape(GpiQueryDevice(handle), 8151, 0, NULL, NULL, NULL);
    dc->print_active = dc->print_page = FALSE;
    return result;
}

LONG AbortDoc(ULONG handle)
{
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !dc->print_active)
        return SP_ERROR;
    LONG result = DevEscape(GpiQueryDevice(handle), 8153, 0, NULL, NULL, NULL);
    dc->print_active = dc->print_page = FALSE;
    return result;
}

BOOL open32PrinterDefaults(unsigned kind, const char *output, PVOID mode)
{
    if (output && *output && strcasecmp(output, "FILE:"))
    {
        open32SetError(50);
        return FALSE;
    }
    if (!mode)
        return TRUE;
    uint8 settings[70];
    if ((uintptr_t)mode > UINT32_MAX ||
        !lx_execution_read(GProcessState.execution, (uint32)(uintptr_t)mode, settings,
                           sizeof settings) ||
        lx_guest_load_u16(settings + 36) < sizeof settings || !memchr(settings, 0, 32))
    {
        open32SetError(87);
        return FALSE;
    }
    if (kind < 1 || kind > 2 || strcasecmp((const char *)settings, lx_printer_queues[kind - 1]))
    {
        open32SetError(1801); /* ERROR_INVALID_PRINTER_NAME */
        return FALSE;
    }
    ULONG fields = lx_guest_load_u32(settings + 40), paper = lx_guest_load_u16(settings + 46),
          orientation = lx_guest_load_u16(settings + 44);
    SHORT quality = (SHORT)lx_guest_load_u16(settings + 58);
    if ((fields & ~0x3d03u) || ((fields & 1) && orientation != 1 && orientation != 2) ||
        ((fields & 2) && paper != 1 && paper != 5 && paper != 9) ||
        ((fields & 0x100) && !lx_guest_load_u16(settings + 54)) ||
        ((fields & 0x400) && quality != 300 && (quality < -4 || quality > -1)) ||
        ((fields & 0x1000) && lx_guest_load_u16(settings + 62) > 3) ||
        ((fields & 0x800) && lx_guest_load_u16(settings + 60) != (kind == 1 ? 2 : 1)) ||
        ((fields & 0x2000) && lx_guest_load_u16(settings + 64) != 300))
    {
        open32SetError(50);
        return FALSE;
    }
    return TRUE;
}

/* The virtual queues print Letter, Legal and A4 at 300 dpi in either orientation. */
BOOL open32PrinterApply(ULONG handle, PVOID mode)
{
    uint8 settings[70];
    if (!mode)
        return TRUE;
    if (!lx_execution_read(GProcessState.execution, (uint32)(uintptr_t)mode, settings,
                           sizeof settings))
    {
        open32SetError(87);
        return FALSE;
    }
    ULONG fields = lx_guest_load_u32(settings + 40);
    ULONG paper = fields & 2 ? lx_guest_load_u16(settings + 46) : 9;
    unsigned width = paper == 9 ? 2480 : 2550, height = paper == 9   ? 3508
                                                        : paper == 5 ? 4200
                                                                     : 3300;
    BOOL landscape = (fields & 1) && lx_guest_load_u16(settings + 44) == 2;
    Open32DC *dc = open32FindDC(handle);
    if (!dc || !lx_pm_printer_page(GpiQueryDevice(handle), landscape ? height : width,
                                   landscape ? width : height))
    {
        open32SetError(8);
        return FALSE;
    }
    if (fields & 0x100)
        dc->print_copies = lx_guest_load_u16(settings + 54);
    if (fields & 0x400)
        dc->print_draft = (SHORT)lx_guest_load_u16(settings + 58) == -1;
    if (fields & 0x1000)
        dc->print_duplex = lx_guest_load_u16(settings + 62);
    return lx_pm_printer_options(GpiQueryDevice(handle), dc->print_copies, dc->print_duplex,
                                 dc->print_draft);
}

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
