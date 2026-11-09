/* Public domain. No rights reserved. Wholly AI-authored. */
/* ChooseFont runs PM's font dialog with the documented Windows hook and template paths. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/comdlg/common_dialogs.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmstddlg.h"
#include "os2/include/private/pmp.h"
#include <stdio.h>

typedef struct FontDialog
{
    ULONG address, hook;
    struct FontDialog *previous;
} FontDialog;

static _Thread_local FontDialog *font_dialogs;

static uint32 font_dispatch(uint8 *a)
{
    ULONG window = lx_guest_load_u32(a), message = lx_guest_load_u32(a + 4);
    ULONG first = lx_guest_load_u32(a + 8), second = lx_guest_load_u32(a + 12);
    FontDialog *dialog = font_dialogs;
    if (dialog && message == WM_INITDLG)
    {
        lx_pm_font_procedure(window, message, (MPARAM)(uintptr_t)first, (MPARAM)(uintptr_t)second);
        lx_execution_callback32_4(GProcessState.execution, dialog->hook, window, WIN_INITDIALOG,
                                  first, dialog->address);
        return 0;
    }
    if (dialog && open32CommonDialogHook(dialog->hook, window, message, first, second))
        return 0;
    return (uint32)(uintptr_t)lx_pm_font_procedure(window, message, (MPARAM)(uintptr_t)first,
                                                   (MPARAM)(uintptr_t)second);
}

BOOL ChooseFont(PVOID record)
{
    if (!record || lx_guest_load_u32(record) != 60)
    {
        open32CommonDialogError(1);
        return FALSE;
    }
    uint8 *wire = record;
    ULONG flags = lx_guest_load_u32(wire + 20), address = lx_guest_load_u32(wire + 12);
    ULONG hook = flags & CF_ENABLEHOOK ? lx_guest_load_u32(wire + 32) : 0;
    if ((flags & CF_ENABLEHOOK) && !open32ValidProcedure(hook))
    {
        open32CommonDialogError(CDERR_NOHOOK);
        return FALSE;
    }
    LxGuestBorrow logfont = {0};
    if (!address || !lx_execution_borrow(GProcessState.execution, address, 60, 3, &logfont))
    {
        open32CommonDialogError(CDERR_INITIALIZATION);
        return FALSE;
    }
    LxGuestMapping *mapping = lx_execution_map(GProcessState.execution, 0, 4096, 3, 0);
    LxGuestMapping *procedure =
        hook ? lx_execution_map(GProcessState.execution, 0, 4096, 3, 0) : NULL;
    PVOID custom = NULL;
    BOOL result = FALSE, opened = FALSE;
    ULONG failure = 0;
    if (!mapping || (hook && !procedure))
    {
        failure = CDERR_MEMALLOCFAILURE;
        goto font_done;
    }
    uint8 *bytes = lx_execution_mapping_host(mapping);
    LXFONTDLG *dialog = (LXFONTDLG *)bytes;
    dialog->cbSize = sizeof *dialog;
    dialog->pszFamilyname = lx_execution_mapping_address(mapping) + 512;
    dialog->usFamilyBufLen = 32;
    memcpy(bytes + 512, (uint8 *)logfont.host + 28, 31);
    bytes[543] = 0;
    LONG points = 100;
    if (flags & CF_INITTOLOGFONTSTRUCT)
    {
        LONG height = (LONG)lx_guest_load_u32(logfont.host);
        if (height < 0)
            height = -height;
        points = height
                     ? height * 720 / (GProcessState.display_dpi ? GProcessState.display_dpi : 96)
                     : 100;
    }
    dialog->fxPointSize = points * 65536 / 10;
    dialog->fl = 1;
    dialog->clrFore = (LONG)lx_guest_load_u32(wire + 24);
    dialog->fAttrs.usRecordLength = sizeof dialog->fAttrs;
    memcpy(dialog->fAttrs.szFacename, bytes + 512, 32);
    if (flags & CF_ENABLETEMPLATE)
    {
        ULONG module = open32DialogTemplateModule(lx_guest_load_u32(wire + 40));
        ULONG identity = open32DialogTemplateIdentity(module, lx_guest_load_u32(wire + 36));
        if (!identity)
        {
            failure = CDERR_FINDRESFAILURE;
            goto font_done;
        }
        dialog->fl |= 2; /* FNTS_CUSTOM */
        dialog->hMod = module;
        dialog->usDlgId = (USHORT)identity;
    }
    else if (flags & CF_ENABLETEMPLATEHANDLE)
    {
        ULONG memory = lx_guest_load_u32(wire + 40), locked = GlobalLock(memory);
        custom = locked ? open32ConvertDialog(locked) : NULL;
        if (locked)
            GlobalUnlock(memory);
        if (!custom)
        {
            failure = CDERR_NOTEMPLATE;
            goto font_done;
        }
    }
    FontDialog state = {
        .address = open32GuestRecordAddress(record, 60), .hook = hook, .previous = font_dialogs};
    if (hook)
    {
        const LxExecutionApi32 api = {font_dispatch, 16, 0, LX_ABI_STACK32};
        if (!lx_execution_emit_bridge32(GProcessState.execution,
                                        lx_execution_mapping_host(procedure), 4096, &api) ||
            !lx_execution_protect(GProcessState.execution, procedure,
                                  LX_GUEST_READ | LX_GUEST_EXECUTE))
        {
            failure = CDERR_MEMALLOCFAILURE;
            goto font_done;
        }
        dialog->pfnDlgProc = lx_execution_mapping_address(procedure);
        font_dialogs = &state;
    }
    PVOID guest = (PVOID)(uintptr_t)lx_execution_mapping_address(mapping);
    ULONG owner = lx_guest_load_u32(wire + 4);
    opened = open32Presentation() &&
             (custom ? lx_pm_font_dialog_template(HWND_DESKTOP, owner, guest, custom)
                     : lx_pm_font_dialog(HWND_DESKTOP, owner, guest));
    if (hook)
        font_dialogs = state.previous;
    result = opened && dialog->lReturn == 1;
    if (result)
    {
        points = (LONG)((int64_t)dialog->fxPointSize * 10 / 65536);
        LONG minimum = (LONG)lx_guest_load_u32(wire + 52);
        LONG maximum = (LONG)lx_guest_load_u32(wire + 56);
        if ((flags & 0x2000) && (points < minimum * 10 || points > maximum * 10))
            result = FALSE;
        else
        {
            uint8 *font = logfont.host;
            lx_guest_store_u32(
                font,
                (ULONG) -
                    (points * (LONG)(GProcessState.display_dpi ? GProcessState.display_dpi : 96) /
                     720));
            lx_guest_store_u32(font + 16, dialog->usWeight >= 7 ? 700 : 400);
            font[20] = (dialog->fAttrs.fsSelection & 1) != 0;
            font[21] = (dialog->fAttrs.fsSelection & 2) != 0;
            font[22] = (dialog->fAttrs.fsSelection & 0x10) != 0;
            memcpy(font + 28, dialog->fAttrs.szFacename, 32);
            font[59] = 0;
            lx_guest_store_u32(wire + 16, points);
            lx_guest_store_u32(wire + 24, (ULONG)dialog->clrFore);
            lx_guest_store_u16(wire + 48, dialog->fAttrs.fsFontUse & 2 ? 4 : 1);
        }
    }
    failure = opened ? 0 : 0xFFFF;
font_done:
    open32CommonDialogError(failure);
    free(custom);
    if (procedure)
        lx_execution_unmap(GProcessState.execution, procedure);
    if (mapping)
        lx_execution_unmap(GProcessState.execution, mapping);
    lx_execution_release(&logfont);
    return result;
}
