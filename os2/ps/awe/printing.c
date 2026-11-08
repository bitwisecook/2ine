/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 common-printer records retain SDK packing and guest heap ownership. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/drawing.h"
#include "os2/ps/pmmerge/pmwin/graphics.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmdev.h"
#include "os2/include/private/pmsplp.h"
#include "host/print.h"

enum
{
    PRINT_DIALOG_BYTES = 66,
    DEVICE_MODE_BYTES = 128
};

static ULONG printer_block(const void *data, size_t size)
{
    ULONG block = GlobalAlloc(0x42, (ULONG)size);
    ULONG address = block ? GlobalLock(block) : 0;
    if (!address || !lx_execution_write(GProcessState.execution, address, data, size))
    {
        if (address)
            GlobalUnlock(block);
        if (block)
            GlobalFree(block);
        return 0;
    }
    GlobalUnlock(block);
    return block;
}

BOOL PrintDlg(PVOID pointer)
{
    open32CommonDialogError(0);
    uint32 address = (uint32)(uintptr_t)pointer;
    uint8 record[PRINT_DIALOG_BYTES];
    if ((uintptr_t)pointer > UINT32_MAX ||
        !lx_execution_read(GProcessState.execution, address, record, sizeof record) ||
        !lx_execution_memory(GProcessState.execution, address, sizeof record, LX_GUEST_WRITE) ||
        lx_guest_load_u32(record) != sizeof record)
    {
        open32CommonDialogError(1); /* CDERR_STRUCTSIZE */
        return FALSE;
    }
    ULONG flags = lx_guest_load_u32(record + 20), old_mode = lx_guest_load_u32(record + 8),
          old_names = lx_guest_load_u32(record + 12);
    if ((flags & 0x3f000) || (flags & 0x300) == 0x300)
    {
        open32SetError(50);           /* Custom guest print hooks/templates are not implemented. */
        open32CommonDialogError(0xb); /* CDERR_NOHOOK */
        return FALSE;
    }
    if ((flags & 0x400) && (old_mode || old_names))
    {
        open32CommonDialogError(0x1003); /* PDERR_RETDEFFAILURE */
        return FALSE;
    }
    int preferred = lx_printer_default_queue();
    if ((flags & 0x400) && preferred < 0)
    {
        open32CommonDialogError(0x1008); /* PDERR_NODEFAULTPRN */
        return FALSE;
    }
    unsigned kind = preferred >= 0 ? (unsigned)preferred + 1 : 1;
    if (!(flags & 0x400))
    {
        LxHostSaveResult choice = lx_host_print_choose_device(&kind);
        if (choice != LX_HOST_SAVE_OK)
        {
            if (choice == LX_HOST_SAVE_ERROR)
                open32CommonDialogError(0x1007); /* PDERR_NODEVICES */
            return FALSE;
        }
    }
    if (kind < 1 || kind > 2)
    {
        open32CommonDialogError(0x1007);
        return FALSE;
    }
    uint8 mode[DEVICE_MODE_BYTES] = {0}, names[128] = {0};
    const char *queue = lx_printer_queues[kind - 1];
    strcpy((char *)mode, queue);
    lx_guest_store_u16(mode + 32, 0x0320);
    lx_guest_store_u16(mode + 34, 1);
    lx_guest_store_u16(mode + 36, sizeof mode);
    lx_guest_store_u32(mode + 40, 0x2c03); /* orientation, A4, quality, color, y-resolution */
    lx_guest_store_u16(mode + 44, 1);
    lx_guest_store_u16(mode + 46, 9);
    lx_guest_store_u16(mode + 54, 1);
    lx_guest_store_u16(mode + 58, 300);
    lx_guest_store_u16(mode + 60, kind == 1 ? 2 : 1);
    lx_guest_store_u16(mode + 64, 300);
    size_t tail = 8;
    for (unsigned i = 0; i < 3; i++)
    {
        const char *value = i == 2 ? "FILE:" : queue;
        lx_guest_store_u16(names + i * 2, (USHORT)tail);
        memcpy(names + tail, value, strlen(value) + 1);
        tail += strlen(value) + 1;
    }
    lx_guest_store_u16(names + 6, (int)kind - 1 == preferred);
    ULONG new_mode = printer_block(mode, sizeof mode), new_names = printer_block(names, tail);
    ULONG dc = flags & 0x300 ? open32CreatePrinterContext(kind, (flags & 0x200) != 0) : 0;
    if (!new_mode || !new_names || ((flags & 0x300) && !dc))
    {
        if (new_mode)
            GlobalFree(new_mode);
        if (new_names)
            GlobalFree(new_names);
        if (dc)
            DeleteDC(dc);
        open32CommonDialogError(9); /* CDERR_MEMALLOCFAILURE */
        return FALSE;
    }
    lx_guest_store_u32(record + 8, new_mode);
    lx_guest_store_u32(record + 12, new_names);
    lx_guest_store_u32(record + 16, dc);
    lx_guest_store_u16(record + 32, 1);
    if (!lx_execution_write(GProcessState.execution, address, record, sizeof record))
    {
        GlobalFree(new_mode);
        GlobalFree(new_names);
        if (dc)
            DeleteDC(dc);
        return FALSE;
    }
    if (old_mode)
        GlobalFree(old_mode);
    if (old_names)
        GlobalFree(old_names);
    return TRUE;
}

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
