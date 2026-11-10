/* Public domain. No rights reserved. Wholly AI-authored. */
/* Dialog helpers: control identities, radio groups, integer items, tab order, dialog units,
   IsDialogMessage and DlgDirSelectEx. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/ps/awe/user/input/keyboard.h"
#include "os2/include/private/pmp.h"
#include "emulator/handles.h"
#include <stdio.h>

enum
{
    /* A dialog unit is a quarter of the font's average width and an eighth of its height. */
    DIALOG_UNITS_PER_CHAR_X = 4,
    DIALOG_UNITS_PER_CHAR_Y = 8,
    /* Tab order visits visible, enabled WS_TABSTOP controls. */
    TAB_STOP_STYLE = WIN32_WS_VISIBLE | WIN32_WS_TABSTOP,
    /* Bounds on a list box item that DlgDirSelectEx reads back. */
    LIST_ITEM_TEXT_MAX = INT16_MAX
};

LONG GetDlgCtrlID(ULONG window)
{
    return (LONG)WinQueryWindowUShort(open32ClientWindow(window), QWS_ID);
}

BOOL CheckRadioButton(ULONG dialog, LONG first, LONG last, LONG checked)
{
    BOOL result = TRUE;
    for (int64_t id = first; id <= last; id++)
        result = CheckDlgButton(dialog, (LONG)id, id == checked);
    return result;
}

ULONG GetDlgItemInt(ULONG dialog, LONG id, PVOID translated, BOOL sign)
{
    char text[64], *end = text;
    ULONG result = 0;
    BOOL success = FALSE;
    /* The buffer is host memory, which GetDlgItemText's guest message cannot address. */
    ULONG item = GetDlgItem(dialog, id);
    if (item && WinQueryWindowText(item, sizeof text, text))
    {
        if (sign)
        {
            long long n = strtoll(text, &end, 10);
            success = end != text && n >= INT32_MIN && n <= INT32_MAX;
            result = (ULONG)n;
        }
        else
        {
            unsigned long long n = strtoull(text, &end, 10);
            success = end != text && *text != '-' && n <= UINT32_MAX;
            result = (ULONG)n;
        }
    }
    if (translated)
        lx_guest_store_u32(translated, success);
    return success ? result : 0;
}

BOOL SetDlgItemInt(ULONG dialog, LONG id, ULONG value, BOOL sign)
{
    char text[32];
    if (sign)
        snprintf(text, sizeof text, "%d", (LONG)value);
    else
        snprintf(text, sizeof text, "%u", value);
    return SetDlgItemText(dialog, id, text);
}

ULONG GetNextDlgTabItem(ULONG dialog, ULONG control, BOOL previous)
{
    ULONG first = WinQueryWindow(open32ClientWindow(dialog), QW_TOP), candidate = 0;
    BOOL passed = !control;
    for (ULONG child = first; child; child = WinQueryWindow(child, QW_NEXT))
    {
        ULONG style = (ULONG)GetWindowLong(child, WIN32_GWL_STYLE);
        BOOL eligible = (style & TAB_STOP_STYLE) == TAB_STOP_STYLE && !(style & WIN32_WS_DISABLED);
        if (child == control)
        {
            if (previous && candidate)
                return candidate;
            passed = TRUE;
            continue;
        }
        if (eligible)
        {
            if (passed && !previous)
                return open32PublicWindow(child);
            candidate = open32PublicWindow(child);
        }
    }
    if (previous)
        return candidate;
    for (ULONG child = first; child && child != control; child = WinQueryWindow(child, QW_NEXT))
    {
        ULONG style = (ULONG)GetWindowLong(child, WIN32_GWL_STYLE);
        if ((style & TAB_STOP_STYLE) == TAB_STOP_STYLE && !(style & WIN32_WS_DISABLED))
            return open32PublicWindow(child);
    }
    return control;
}

BOOL MapDialogRect(ULONG dialog, PVOID rectangle)
{
    if (!rectangle || !IsWindow(dialog))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG units = GetDialogBaseUnits();
    for (unsigned i = 0; i < 4; i++)
    {
        LONG n = (LONG)lx_guest_load_u32((uint8 *)rectangle + i * 4);
        LONG unit = i & 1 ? units >> 16 : units & 0xFFFF;
        lx_guest_store_u32((uint8 *)rectangle + i * 4,
                           (ULONG)((int64_t)n * unit /
                                   (i & 1 ? DIALOG_UNITS_PER_CHAR_Y : DIALOG_UNITS_PER_CHAR_X)));
    }
    return TRUE;
}

BOOL IsDialogMessage(ULONG dialog, PVOID message)
{
    if (!message || !IsWindow(dialog))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG target = lx_guest_load_u32((uint8 *)message + WIN32_MSG_HWND),
          kind = lx_guest_load_u32((uint8 *)message + WIN32_MSG_MESSAGE),
          key = lx_guest_load_u32((uint8 *)message + WIN32_MSG_WPARAM);
    if (target != dialog && !IsChild(dialog, target))
        return FALSE;
    if (kind == WIN32_WM_KEYDOWN)
    {
        ULONG code = (ULONG)SendMessage(target, WIN32_WM_GETDLGCODE, key, 0);
        if (code & WIN32_DLGC_WANTALLKEYS)
            return FALSE;
        if (key == WIN32_VK_TAB && !(code & WIN32_DLGC_WANTTAB))
        {
            /* The high bit of a key state means the key is down. */
            ULONG next =
                GetNextDlgTabItem(dialog, target, (GetKeyState(WIN32_VK_SHIFT) & 0x8000) != 0);
            if (next)
                SetFocus(next);
            return TRUE;
        }
        if ((key == WIN32_VK_RETURN || key == WIN32_VK_ESCAPE) && !(code & WIN32_DLGC_WANTALLKEYS))
        {
            ULONG id = key == WIN32_VK_RETURN ? WIN32_IDOK : WIN32_IDCANCEL;
            SendMessage(dialog, WIN32_WM_COMMAND, id, GetDlgItem(dialog, id));
            return TRUE;
        }
    }
    TranslateMessage(message);
    DispatchMessage(message);
    return TRUE;
}

BOOL DlgDirSelectEx(ULONG dialog, PSZ output, LONG capacity, LONG id)
{
    if (!output || capacity <= 0)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    LONG index = SendDlgItemMessage(dialog, id, WIN32_LB_GETCURSEL, 0, 0);
    if (index < 0)
    {
        output[0] = 0;
        return FALSE;
    }
    LONG length = SendDlgItemMessage(dialog, id, WIN32_LB_GETTEXTLEN, index, 0);
    if (length < 0 || length > LIST_ITEM_TEXT_MAX)
    {
        output[0] = 0;
        return FALSE;
    }
    LxGuestMapping *mapping =
        lx_execution_map(GProcessState.execution, 0,
                         ((size_t)length + OPEN32_PAGE_SIZE) & ~(size_t)(OPEN32_PAGE_SIZE - 1),
                         LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!mapping)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    char *text = lx_execution_mapping_host(mapping);
    SendDlgItemMessage(dialog, id, WIN32_LB_GETTEXT, index, lx_execution_mapping_address(mapping));
    /* DlgDirList lists directories as [name] and drives as [-x-]. */
    BOOL directory = length > 1 && text[0] == '[' && text[length - 1] == ']';
    if (directory)
    {
        text[length - 1] = 0;
        memmove(text, text + 1, length--);
        if (length >= 3 && text[0] == '-' && text[length - 2] == '-')
        {
            text[0] = text[1];
            text[1] = ':';
            text[2] = 0;
        }
        else
        {
            text[length - 1] = '\\';
            text[length] = 0;
        }
    }
    size_t count = strlen(text);
    if (count >= (size_t)capacity)
        count = capacity - 1;
    memcpy(output, text, count);
    output[count] = 0;
    lx_execution_unmap(GProcessState.execution, mapping);
    return directory;
}
