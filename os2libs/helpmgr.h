/* Public domain. No rights reserved. Wholly AI-authored. */
/* Help-manager creation reports unavailable rendering through HELPINIT. */
#ifndef TWOINE_OS2LIBS_HELPMGR_H
#define TWOINE_OS2LIBS_HELPMGR_H
#include "pmwin.h"
#ifdef __cplusplus
extern "C"
{
#endif
    OS2EXPORT HWND OS2API WinCreateHelpInstance(HAB hab, PVOID init) OS2APIINFO(51);
    OS2EXPORT BOOL OS2API WinDestroyHelpInstance(HWND help) OS2APIINFO(52);
    OS2EXPORT BOOL OS2API WinAssociateHelpInstance(HWND help, HWND window) OS2APIINFO(54);
#ifdef __cplusplus
}
#endif
#endif
