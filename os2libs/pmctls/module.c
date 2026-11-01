/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2libs/os2native.h"
#include "os2libs/pmctls.h"
#include "os2libs/pmwin/standard_dialogs.h"
#include "os2libs/generated/pmctls-lx.h"
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
