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

LONG GetDlgCtrlID(ULONG window)
{
    return (LONG)WinQueryWindowUShort(open32ClientWindow(window), -1);
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
    ULONG first = WinQueryWindow(open32ClientWindow(dialog), 2), candidate = 0;
    BOOL passed = !control;
    for (ULONG child = first; child; child = WinQueryWindow(child, 0))
    {
        ULONG style = (ULONG)GetWindowLong(child, -16);
        BOOL eligible = (style & 0x10010000u) == 0x10010000u && !(style & 0x08000000u);
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
    for (ULONG child = first; child && child != control; child = WinQueryWindow(child, 0))
    {
        ULONG style = (ULONG)GetWindowLong(child, -16);
        if ((style & 0x10010000u) == 0x10010000u && !(style & 0x08000000u))
            return open32PublicWindow(child);
    }
    return control;
}

BOOL MapDialogRect(ULONG dialog, PVOID rectangle)
{
    if (!rectangle || !IsWindow(dialog))
    {
        open32SetError(87);
        return FALSE;
    }
    ULONG units = GetDialogBaseUnits();
    for (unsigned i = 0; i < 4; i++)
    {
        LONG n = (LONG)lx_guest_load_u32((uint8 *)rectangle + i * 4);
        LONG unit = i & 1 ? units >> 16 : units & 0xFFFF;
        lx_guest_store_u32((uint8 *)rectangle + i * 4,
                           (ULONG)((int64_t)n * unit / (i & 1 ? 8 : 4)));
    }
    return TRUE;
}

BOOL IsDialogMessage(ULONG dialog, PVOID message)
{
    if (!message || !IsWindow(dialog))
    {
        open32SetError(87);
        return FALSE;
    }
    ULONG target = lx_guest_load_u32(message), kind = lx_guest_load_u32((uint8 *)message + 4),
          key = lx_guest_load_u32((uint8 *)message + 8);
    if (target != dialog && !IsChild(dialog, target))
        return FALSE;
    if (kind == 0x100)
    {
        ULONG code = (ULONG)SendMessage(target, 0x87, key, 0);
        if (code & 4)
            return FALSE;
        if (key == 9 && !(code & 2))
        {
            ULONG next = GetNextDlgTabItem(dialog, target, (GetKeyState(16) & 0x8000) != 0);
            if (next)
                SetFocus(next);
            return TRUE;
        }
        if ((key == 13 || key == 27) && !(code & 4))
        {
            ULONG id = key == 13 ? 1 : 2;
            SendMessage(dialog, 0x111, id, GetDlgItem(dialog, id));
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
        open32SetError(87);
        return FALSE;
    }
    LONG index = SendDlgItemMessage(dialog, id, 0x188, 0, 0);
    if (index < 0)
    {
        output[0] = 0;
        return FALSE;
    }
    LONG length = SendDlgItemMessage(dialog, id, 0x18A, index, 0);
    if (length < 0 || length > 32767)
    {
        output[0] = 0;
        return FALSE;
    }
    LxGuestMapping *mapping =
        lx_execution_map(GProcessState.execution, 0, ((size_t)length + 4096) & ~(size_t)4095, 3, 0);
    if (!mapping)
    {
        open32SetError(8);
        return FALSE;
    }
    char *text = lx_execution_mapping_host(mapping);
    SendDlgItemMessage(dialog, id, 0x189, index, lx_execution_mapping_address(mapping));
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
