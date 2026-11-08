/* Public domain. No rights reserved. Wholly AI-authored. */
/* Help-manager creation reports unavailable rendering through HELPINIT. */
#ifndef _INCL_PMHELP_H_
#define _INCL_PMHELP_H_

#include "os2/include/pmwin.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* INCL_WINHELP: help manager */

    OS2EXPORT HWND OS2API WinCreateHelpInstance(HAB hab, PVOID init);

    OS2EXPORT BOOL OS2API WinDestroyHelpInstance(HWND help);

    OS2EXPORT BOOL OS2API WinAssociateHelpInstance(HWND help, HWND window);

#ifdef __cplusplus
}
#endif

#endif
