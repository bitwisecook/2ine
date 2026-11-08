/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2/include/private/export.h"
#include "os2/include/pmstddlg.h"
#include "os2/ps/pmwin/standard_dialogs.h"
#include "os2/ps/controls/pmctls/generated/pmctls-lx.h"

HWND WinFileDlg(HWND parent, HWND owner, PVOID data)
{
    return lx_pm_file_dialog(parent, owner, data);
}

MRESULT WinDefFileDlgProc(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    TRACE_NATIVE("WinDefFileDlgProc(%u, %u, %p, %p)", (uint)window, (uint)message, first, second);
    return lx_pm_file_procedure(window, message, first, second);
}

BOOL WinFreeFileDlgList(PVOID list)
{
    return list == NULL;
}

HWND WinFontDlg(HWND parent, HWND owner, PVOID data)
{
    return lx_pm_font_dialog(parent, owner, data);
}

MRESULT WinDefFontDlgProc(HWND window, ULONG message, MPARAM first, MPARAM second)
{
    return lx_pm_font_procedure(window, message, first, second);
}
