/* Public domain. No rights reserved. Wholly AI-authored. */
/* Standard dialogs execute through PM windows, controls and the real modal loop. */
#ifndef TWOINE_PM_STANDARD_DIALOGS_H
#define TWOINE_PM_STANDARD_DIALOGS_H
#include "os2libs/pmwin.h"
HWND lx_pm_file_dialog(HWND parent, HWND owner, PVOID data);
BOOL lx_pm_file_set_filter(HWND window, const char *pattern);
MRESULT lx_pm_file_procedure(HWND window, ULONG message, MPARAM first, MPARAM second);
#endif
