/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2native.h"
#include "helpmgr.h"
#include "guest/abi.h"
#include "os2libs/generated/helpmgr-lx.h"

HWND WinCreateHelpInstance(HAB hab, PVOID init)
{
    TRACE_NATIVE("WinCreateHelpInstance(%u, %p)", (uint)hab, init);
    if (init && lx_guest_load_u32(init) == 44)
        /* HMERR_LOAD_DLL: no information-presentation renderer is available. */
        lx_guest_store_u32((uint8 *)init + 4, 0x2013);
    return NULLHANDLE;
}

BOOL WinDestroyHelpInstance(HWND help)
{
    TRACE_NATIVE("WinDestroyHelpInstance(%u)", (uint)help);
    return FALSE;
}

BOOL WinAssociateHelpInstance(HWND help, HWND window)
{
    TRACE_NATIVE("WinAssociateHelpInstance(%u, %u)", (uint)help, (uint)window);
    return FALSE;
}
