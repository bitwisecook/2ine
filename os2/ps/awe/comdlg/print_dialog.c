/* Public domain. No rights reserved. Wholly AI-authored. */
/* PrintDlg chooses a printer from the configured queues and returns DEVMODE and DEVNAMES blocks. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/gdi/dc.h"
#include "os2/include/pmdev.h"
#include "os2/include/private/pmsplp.h"
#include "host/print.h"

enum
{
    /* The DEVMODE and DEVNAMES blocks returned, and the dpi reported in both
       directions. */
    DEVICE_MODE_BYTES = 128,
    DEVICE_NAMES_BYTES = 128,
    PRINTER_DPI = 300,
    /* PD_ENABLEPRINTHOOK through PD_ENABLESETUPTEMPLATEHANDLE. */
    PRINT_DIALOG_CUSTOMIZATION = WIN32_PD_ENABLEPRINTHOOK | WIN32_PD_ENABLESETUPHOOK |
                                 WIN32_PD_ENABLEPRINTTEMPLATE | WIN32_PD_ENABLESETUPTEMPLATE |
                                 WIN32_PD_ENABLEPRINTTEMPLATEHANDLE |
                                 WIN32_PD_ENABLESETUPTEMPLATEHANDLE,
    PRINT_DIALOG_RETURN_CONTEXT = WIN32_PD_RETURNDC | WIN32_PD_RETURNIC
};

static ULONG printer_block(const void *data, size_t size)
{
    ULONG block = GlobalAlloc(WIN32_GMEM_MOVEABLE | WIN32_GMEM_ZEROINIT, (ULONG)size);
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
    uint8 record[WIN32_PRINTDLG_SIZE];
    if ((uintptr_t)pointer > UINT32_MAX ||
        !lx_execution_read(GProcessState.execution, address, record, sizeof record) ||
        !lx_execution_memory(GProcessState.execution, address, sizeof record, LX_GUEST_WRITE) ||
        lx_guest_load_u32(record) != sizeof record)
    {
        open32CommonDialogError(WIN32_CDERR_STRUCTSIZE);
        return FALSE;
    }
    ULONG flags = lx_guest_load_u32(record + WIN32_PRINTDLG_FLAGS),
          old_mode = lx_guest_load_u32(record + WIN32_PRINTDLG_DEVMODE),
          old_names = lx_guest_load_u32(record + WIN32_PRINTDLG_DEVNAMES);
    if ((flags & PRINT_DIALOG_CUSTOMIZATION) ||
        (flags & PRINT_DIALOG_RETURN_CONTEXT) == PRINT_DIALOG_RETURN_CONTEXT)
    {
        /* Guest print hooks and templates are not implemented. */
        open32SetError(ERROR_NOT_SUPPORTED);
        open32CommonDialogError(WIN32_CDERR_NOHOOK);
        return FALSE;
    }
    if ((flags & WIN32_PD_RETURNDEFAULT) && (old_mode || old_names))
    {
        open32CommonDialogError(WIN32_PDERR_RETDEFFAILURE);
        return FALSE;
    }
    int preferred = lx_printer_default_queue();
    if ((flags & WIN32_PD_RETURNDEFAULT) && preferred < 0)
    {
        open32CommonDialogError(WIN32_PDERR_NODEFAULTPRN);
        return FALSE;
    }
    /* kind numbers the 2ine printer queues from 1. */
    unsigned kind = preferred >= 0 ? (unsigned)preferred + 1 : 1;
    if (!(flags & WIN32_PD_RETURNDEFAULT))
    {
        LxHostSaveResult choice = lx_host_print_choose_device(&kind);
        if (choice != LX_HOST_SAVE_OK)
        {
            if (choice == LX_HOST_SAVE_ERROR)
                open32CommonDialogError(WIN32_PDERR_NODEVICES);
            return FALSE;
        }
    }
    if (kind < 1 || kind > LX_PRINTER_COUNT)
    {
        open32CommonDialogError(WIN32_PDERR_NODEVICES);
        return FALSE;
    }
    uint8 mode[DEVICE_MODE_BYTES] = {0}, names[DEVICE_NAMES_BYTES] = {0};
    const char *queue = lx_printer_queues[kind - 1];
    strcpy((char *)mode, queue);
    lx_guest_store_u16(mode + WIN32_DEVMODE_SPEC_VERSION, WIN32_DM_SPECVERSION);
    lx_guest_store_u16(mode + WIN32_DEVMODE_DRIVER_VERSION, 1);
    lx_guest_store_u16(mode + WIN32_DEVMODE_SIZE, sizeof mode);
    lx_guest_store_u32(mode + WIN32_DEVMODE_FIELDS, WIN32_DM_ORIENTATION | WIN32_DM_PAPERSIZE |
                                                        WIN32_DM_PRINTQUALITY | WIN32_DM_COLOR |
                                                        WIN32_DM_YRESOLUTION);
    lx_guest_store_u16(mode + WIN32_DEVMODE_ORIENTATION, WIN32_DMORIENT_PORTRAIT);
    lx_guest_store_u16(mode + WIN32_DEVMODE_PAPER_SIZE, WIN32_DMPAPER_A4);
    lx_guest_store_u16(mode + WIN32_DEVMODE_COPIES, 1);
    lx_guest_store_u16(mode + WIN32_DEVMODE_PRINT_QUALITY, PRINTER_DPI);
    /* The first queue (PDF) prints in colour, the second (PCL) in monochrome. */
    lx_guest_store_u16(mode + WIN32_DEVMODE_COLOR,
                       kind == 1 ? WIN32_DMCOLOR_COLOR : WIN32_DMCOLOR_MONOCHROME);
    lx_guest_store_u16(mode + WIN32_DEVMODE_Y_RESOLUTION, PRINTER_DPI);
    /* DEVNAMES: three offsets (driver, device, output) and wDefault, then the strings. */
    size_t tail = WIN32_DEVNAMES_SIZE;
    for (unsigned i = 0; i < 3; i++)
    {
        const char *value = i == 2 ? "FILE:" : queue;
        lx_guest_store_u16(names + i * (unsigned)sizeof(USHORT), (USHORT)tail);
        memcpy(names + tail, value, strlen(value) + 1);
        tail += strlen(value) + 1;
    }
    lx_guest_store_u16(names + WIN32_DEVNAMES_DEFAULT, (int)kind - 1 == preferred);
    ULONG new_mode = printer_block(mode, sizeof mode), new_names = printer_block(names, tail);
    ULONG dc = flags & PRINT_DIALOG_RETURN_CONTEXT
                   ? open32CreatePrinterContext(kind, (flags & WIN32_PD_RETURNIC) != 0)
                   : 0;
    if (!new_mode || !new_names || ((flags & PRINT_DIALOG_RETURN_CONTEXT) && !dc))
    {
        if (new_mode)
            GlobalFree(new_mode);
        if (new_names)
            GlobalFree(new_names);
        if (dc)
            DeleteDC(dc);
        open32CommonDialogError(WIN32_CDERR_MEMALLOCFAILURE);
        return FALSE;
    }
    lx_guest_store_u32(record + WIN32_PRINTDLG_DEVMODE, new_mode);
    lx_guest_store_u32(record + WIN32_PRINTDLG_DEVNAMES, new_names);
    lx_guest_store_u32(record + WIN32_PRINTDLG_DC, dc);
    lx_guest_store_u16(record + WIN32_PRINTDLG_COPIES, 1);
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
