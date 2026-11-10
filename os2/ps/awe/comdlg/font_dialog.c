/* Public domain. No rights reserved. Wholly AI-authored. */
/* ChooseFont runs PM's font dialog with the documented Windows hook and template paths. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/comdlg/common_dialogs.h"
#include "os2/include/pmgpi.h"
#include "os2/include/pmstddlg.h"
#include "os2/include/private/pmp.h"
#include <stdio.h>

enum
{
    /* The guest page holding the PM FONTDLG record, with the family-name buffer after it. */
    FONTDLG_FAMILY_NAME = 512,
    /* Point sizes are in tenths here; FONTDLG holds a 16.16 FIXED point size. */
    TENTHS_PER_POINT = 10,
    FIXED_ONE = 65536,
    TENTHS_PER_INCH = 72 * TENTHS_PER_POINT,
    DEFAULT_DPI = 96,
    DEFAULT_POINT_SIZE_TENTHS = 100,
    FONT_PROCEDURE_ARGUMENT_BYTES = 4 * sizeof(uint32)
};

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
        lx_execution_callback32_4(GProcessState.execution, dialog->hook, window,
                                  WIN32_WM_INITDIALOG, first, dialog->address);
        return 0;
    }
    if (dialog && open32CommonDialogHook(dialog->hook, window, message, first, second))
        return 0;
    return (uint32)(uintptr_t)lx_pm_font_procedure(window, message, (MPARAM)(uintptr_t)first,
                                                   (MPARAM)(uintptr_t)second);
}

BOOL ChooseFont(PVOID record)
{
    if (!record || lx_guest_load_u32(record) != WIN32_CHOOSEFONT_SIZE)
    {
        open32CommonDialogError(WIN32_CDERR_STRUCTSIZE);
        return FALSE;
    }
    uint8 *wire = record;
    ULONG flags = lx_guest_load_u32(wire + WIN32_CHOOSEFONT_FLAGS),
          address = lx_guest_load_u32(wire + WIN32_CHOOSEFONT_LOGFONT);
    ULONG hook = flags & WIN32_CF_ENABLEHOOK ? lx_guest_load_u32(wire + WIN32_CHOOSEFONT_HOOK) : 0;
    if ((flags & WIN32_CF_ENABLEHOOK) && !open32ValidProcedure(hook))
    {
        open32CommonDialogError(WIN32_CDERR_NOHOOK);
        return FALSE;
    }
    LxGuestBorrow logfont = {0};
    if (!address || !lx_execution_borrow(GProcessState.execution, address, WIN32_LOGFONT_SIZE,
                                         LX_GUEST_READ | LX_GUEST_WRITE, &logfont))
    {
        open32CommonDialogError(WIN32_CDERR_INITIALIZATION);
        return FALSE;
    }
    LxGuestMapping *mapping = lx_execution_map(GProcessState.execution, 0, OPEN32_PAGE_SIZE,
                                               LX_GUEST_READ | LX_GUEST_WRITE, 0);
    LxGuestMapping *procedure = hook
                                    ? lx_execution_map(GProcessState.execution, 0, OPEN32_PAGE_SIZE,
                                                       LX_GUEST_READ | LX_GUEST_WRITE, 0)
                                    : NULL;
    PVOID custom = NULL;
    BOOL result = FALSE, opened = FALSE;
    ULONG failure = 0;
    if (!mapping || (hook && !procedure))
    {
        failure = WIN32_CDERR_MEMALLOCFAILURE;
        goto font_done;
    }
    uint8 *bytes = lx_execution_mapping_host(mapping);
    LXFONTDLG *dialog = (LXFONTDLG *)bytes;
    dialog->cbSize = sizeof *dialog;
    dialog->pszFamilyname = lx_execution_mapping_address(mapping) + FONTDLG_FAMILY_NAME;
    dialog->usFamilyBufLen = FACESIZE;
    memcpy(bytes + FONTDLG_FAMILY_NAME, (uint8 *)logfont.host + WIN32_LOGFONT_FACE_NAME,
           FACESIZE - 1);
    bytes[FONTDLG_FAMILY_NAME + FACESIZE - 1] = 0;
    LONG points = DEFAULT_POINT_SIZE_TENTHS;
    if (flags & WIN32_CF_INITTOLOGFONTSTRUCT)
    {
        LONG height = (LONG)lx_guest_load_u32(logfont.host);
        if (height < 0)
            height = -height;
        points = height ? height * TENTHS_PER_INCH /
                              (GProcessState.display_dpi ? GProcessState.display_dpi : DEFAULT_DPI)
                        : DEFAULT_POINT_SIZE_TENTHS;
    }
    dialog->fxPointSize = points * FIXED_ONE / TENTHS_PER_POINT;
    dialog->fl = FNTS_CENTER;
    dialog->clrFore = (LONG)lx_guest_load_u32(wire + WIN32_CHOOSEFONT_COLORS);
    dialog->fAttrs.usRecordLength = sizeof dialog->fAttrs;
    memcpy(dialog->fAttrs.szFacename, bytes + FONTDLG_FAMILY_NAME, FACESIZE);
    if (flags & WIN32_CF_ENABLETEMPLATE)
    {
        ULONG module =
            open32DialogTemplateModule(lx_guest_load_u32(wire + WIN32_CHOOSEFONT_INSTANCE));
        ULONG identity = open32DialogTemplateIdentity(
            module, lx_guest_load_u32(wire + WIN32_CHOOSEFONT_TEMPLATE_NAME));
        if (!identity)
        {
            failure = WIN32_CDERR_FINDRESFAILURE;
            goto font_done;
        }
        dialog->fl |= FNTS_CUSTOM;
        dialog->hMod = module;
        dialog->usDlgId = (USHORT)identity;
    }
    else if (flags & WIN32_CF_ENABLETEMPLATEHANDLE)
    {
        ULONG memory = lx_guest_load_u32(wire + WIN32_CHOOSEFONT_INSTANCE),
              locked = GlobalLock(memory);
        custom = locked ? open32ConvertDialog(locked) : NULL;
        if (locked)
            GlobalUnlock(memory);
        if (!custom)
        {
            failure = WIN32_CDERR_NOTEMPLATE;
            goto font_done;
        }
    }
    FontDialog state = {.address = open32GuestRecordAddress(record, WIN32_CHOOSEFONT_SIZE),
                        .hook = hook,
                        .previous = font_dialogs};
    if (hook)
    {
        const LxExecutionApi32 api = {font_dispatch, FONT_PROCEDURE_ARGUMENT_BYTES, 0,
                                      LX_ABI_STACK32};
        if (!lx_execution_emit_bridge32(GProcessState.execution,
                                        lx_execution_mapping_host(procedure), OPEN32_PAGE_SIZE,
                                        &api) ||
            !lx_execution_protect(GProcessState.execution, procedure,
                                  LX_GUEST_READ | LX_GUEST_EXECUTE))
        {
            failure = WIN32_CDERR_MEMALLOCFAILURE;
            goto font_done;
        }
        dialog->pfnDlgProc = lx_execution_mapping_address(procedure);
        font_dialogs = &state;
    }
    PVOID guest = (PVOID)(uintptr_t)lx_execution_mapping_address(mapping);
    ULONG owner = lx_guest_load_u32(wire + WIN32_CHOOSEFONT_OWNER);
    opened = open32Presentation() &&
             (custom ? lx_pm_font_dialog_template(HWND_DESKTOP, owner, guest, custom)
                     : lx_pm_font_dialog(HWND_DESKTOP, owner, guest));
    if (hook)
        font_dialogs = state.previous;
    result = opened && dialog->lReturn == DID_OK;
    if (result)
    {
        points = (LONG)((int64_t)dialog->fxPointSize * TENTHS_PER_POINT / FIXED_ONE);
        LONG minimum = (LONG)lx_guest_load_u32(wire + WIN32_CHOOSEFONT_SIZE_MIN);
        LONG maximum = (LONG)lx_guest_load_u32(wire + WIN32_CHOOSEFONT_SIZE_MAX);
        if ((flags & WIN32_CF_LIMITSIZE) &&
            (points < minimum * TENTHS_PER_POINT || points > maximum * TENTHS_PER_POINT))
            result = FALSE;
        else
        {
            uint8 *font = logfont.host;
            lx_guest_store_u32(
                font, (ULONG) - (points *
                                 (LONG)(GProcessState.display_dpi ? GProcessState.display_dpi
                                                                  : DEFAULT_DPI) /
                                 TENTHS_PER_INCH));
            lx_guest_store_u32(font + WIN32_LOGFONT_WEIGHT,
                               dialog->usWeight >= FWEIGHT_BOLD ? WIN32_FW_BOLD : WIN32_FW_NORMAL);
            font[WIN32_LOGFONT_ITALIC] = (dialog->fAttrs.fsSelection & FATTR_SEL_ITALIC) != 0;
            font[WIN32_LOGFONT_UNDERLINE] =
                (dialog->fAttrs.fsSelection & FATTR_SEL_UNDERSCORE) != 0;
            font[WIN32_LOGFONT_STRIKE_OUT] =
                (dialog->fAttrs.fsSelection & FATTR_SEL_STRIKEOUT) != 0;
            memcpy(font + WIN32_LOGFONT_FACE_NAME, dialog->fAttrs.szFacename, FACESIZE);
            font[WIN32_LOGFONT_FACE_NAME + FACESIZE - 1] = 0;
            lx_guest_store_u32(wire + WIN32_CHOOSEFONT_POINT_SIZE, points);
            lx_guest_store_u32(wire + WIN32_CHOOSEFONT_COLORS, (ULONG)dialog->clrFore);
            lx_guest_store_u16(wire + WIN32_CHOOSEFONT_FONT_TYPE,
                               dialog->fAttrs.fsFontUse & FATTR_FONTUSE_NOMIX
                                   ? WIN32_TRUETYPE_FONTTYPE
                                   : WIN32_RASTER_FONTTYPE);
        }
    }
    failure = opened ? 0 : WIN32_CDERR_DIALOGFAILURE;
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
