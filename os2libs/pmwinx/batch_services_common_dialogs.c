/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/batch_services.h"
#include "os2libs/pmctls.h"
#include "os2libs/pmwin/standard_dialogs.h"
#include <stdio.h>
BOOL ChooseFont(PVOID record)
{
    if (!record || lx_guest_load_u32(record) != 60)
    {
        open32CommonDialogError(1);
        return FALSE;
    }
    uint8 *wire = record;
    ULONG flags = lx_guest_load_u32(wire + 20), address = lx_guest_load_u32(wire + 12);
    if (flags & (8 | 0x10 | 0x20))
    {
        open32CommonDialogError(6);
        return FALSE;
    }
    LxGuestBorrow logfont = {0};
    if (!address || !lx_execution_borrow(GLoaderState.execution, address, 60, 3, &logfont))
    {
        open32CommonDialogError(2);
        return FALSE;
    }
    LxGuestMapping *mapping = lx_execution_map(GLoaderState.execution, 0, 4096, 3, 0);
    if (!mapping)
    {
        lx_execution_release(&logfont);
        open32CommonDialogError(9);
        return FALSE;
    }
    uint8 *bytes = lx_execution_mapping_host(mapping);
    LXFONTDLG *dialog = (LXFONTDLG *)bytes;
    dialog->cbSize = sizeof *dialog;
    dialog->pszFamilyname = lx_execution_mapping_address(mapping) + 512;
    dialog->usFamilyBufLen = 32;
    memcpy(bytes + 512, (uint8 *)logfont.host + 28, 31);
    bytes[543] = 0;
    LONG points = 100;
    if (flags & 0x40) /* CF_INITTOLOGFONTSTRUCT */
    {
        LONG height = (LONG)lx_guest_load_u32(logfont.host);
        if (height < 0)
            height = -height;
        points = height ? height * 720 / (GLoaderState.display_dpi ? GLoaderState.display_dpi : 96)
                        : 100;
    }
    dialog->fxPointSize = points * 65536 / 10;
    dialog->fl = 1;
    dialog->fAttrs.usRecordLength = sizeof dialog->fAttrs;
    memcpy(dialog->fAttrs.szFacename, bytes + 512, 32);
    BOOL opened = open32Presentation() &&
                  lx_pm_font_dialog(HWND_DESKTOP, lx_guest_load_u32(wire + 4),
                                    (PVOID)(uintptr_t)lx_execution_mapping_address(mapping));
    BOOL result = opened && dialog->lReturn == 1;
    if (result)
    {
        points = (LONG)((int64_t)dialog->fxPointSize * 10 / 65536);
        LONG min = (LONG)lx_guest_load_u32(wire + 52), max = (LONG)lx_guest_load_u32(wire + 56);
        if ((flags & 0x2000) && (points < min * 10 || points > max * 10))
            result = FALSE;
        else
        {
            uint8 *font = logfont.host;
            lx_guest_store_u32(
                font,
                (ULONG) - (points *
                           (LONG)(GLoaderState.display_dpi ? GLoaderState.display_dpi : 96) / 720));
            lx_guest_store_u32(font + 16, dialog->usWeight >= 7 ? 700 : 400);
            font[20] = (dialog->fAttrs.fsSelection & 1) != 0;
            memcpy(font + 28, dialog->fAttrs.szFacename, 32);
            font[59] = 0;
            lx_guest_store_u32(wire + 16, points);
            lx_guest_store_u16(wire + 48, dialog->fAttrs.fsFontUse & 2 ? 4 : 1);
        }
    }
    if (!opened)
        open32CommonDialogError(0xFFFF);
    else
        open32CommonDialogError(0);
    lx_execution_unmap(GLoaderState.execution, mapping);
    lx_execution_release(&logfont);
    return result;
}
typedef struct ColorDialog
{
    ULONG color;
    BOOL accepted;
    struct ColorDialog *previous;
} ColorDialog;
static _Thread_local ColorDialog *color_dialog;
static MRESULT OS2API color_procedure(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    ColorDialog *dialog = color_dialog;
    if (!dialog)
        return WinDefDlgProc(window, message, first, second);
    if (message == WM_INITDLG)
    {
        char value[16];
        snprintf(value, sizeof value, "%06X", dialog->color & 0xFFFFFF);
        WinSetDlgItemText(window, 101, value);
        return 0;
    }
    if (message == WM_COMMAND && (USHORT)(uintptr_t)first == 1)
    {
        char value[32], *end;
        WinQueryDlgItemText(window, 101, sizeof value, value);
        unsigned long color = strtoul(value, &end, 16);
        if (end != value && !*end && color <= 0xFFFFFF)
        {
            dialog->color = color;
            dialog->accepted = TRUE;
            WinDismissDlg(window, 1);
        }
        return 0;
    }
    return WinDefDlgProc(window, message, first, second);
}
BOOL ChooseColor(PVOID record)
{
    if (!record || lx_guest_load_u32(record) != 36)
    {
        open32CommonDialogError(1);
        return FALSE;
    }
    uint8 *wire = record;
    ULONG flags = lx_guest_load_u32(wire + 20);
    if (flags & (0x10 | 0x20 | 0x40))
    {
        open32CommonDialogError(6);
        return FALSE;
    }
    uint8 bytes[512] = {0};
    DLGTEMPLATE *t = (void *)bytes;
    DLGTITEM *items = (void *)(bytes + 14);
    t->codepage = 437;
    t->offadlgti = 14;
    t->iItemFocus = 2;
    items[0].cChildren = 4;
    items[0].offClassName = 1;
    items[0].cx = 220;
    items[0].cy = 90;
    items[0].flStyle = WS_VISIBLE;
    const char *text[] = {"Color", "Color (BBGGRR hexadecimal):", "", "OK", "Cancel"};
    USHORT kind[] = {1, 5, 6, 3, 3}, ids[] = {100, 102, 101, 1, 2};
    SHORT x[] = {0, 8, 8, 48, 130}, y[] = {0, 62, 42, 12, 12}, width[] = {220, 205, 205, 64, 64},
          height[] = {90, 12, 14, 14, 14};
    size_t offset = 14 + 5 * 30;
    for (unsigned i = 0; i < 5; i++)
    {
        DLGTITEM *item = items + i;
        item->id = ids[i];
        item->offClassName = kind[i];
        item->x = x[i];
        item->y = y[i];
        item->cx = width[i];
        item->cy = height[i];
        item->flStyle = WS_VISIBLE | (i > 1 ? 0x20000 : 0) | (i == 3 ? 0x400 : 0);
        item->offPresParams = item->offCtlData = 0xFFFF;
        item->cchText = strlen(text[i]);
        item->offText = offset;
        memcpy(bytes + offset, text[i], strlen(text[i]) + 1);
        offset += strlen(text[i]) + 1;
    }
    t->cbTemplate = offset;
    ColorDialog dialog = {.color = flags & 1 ? lx_guest_load_u32(wire + 12) : 0,
                          .previous = color_dialog};
    color_dialog = &dialog;
    ULONG window = open32Presentation() ? WinCreateDlg(HWND_DESKTOP, lx_guest_load_u32(wire + 4),
                                                       color_procedure, t, NULL)
                                        : 0;
    if (window)
    {
        WinProcessDlg(window);
        WinDestroyWindow(window);
    }
    color_dialog = dialog.previous;
    if (dialog.accepted)
        lx_guest_store_u32(wire + 12, dialog.color);
    open32CommonDialogError(window ? 0 : 0xFFFF);
    return dialog.accepted;
}
