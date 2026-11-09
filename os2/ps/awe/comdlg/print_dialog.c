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
