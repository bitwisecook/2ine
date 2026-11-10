/* Public domain. No rights reserved. Wholly AI-authored. */
/* HELPMGR: help instances cannot be created, so applications run without help windows. */
#include "os2/include/private/export.h"
#include "os2/include/pmhelp.h"
#include "emulator/abi.h"

/* The 32-bit guest HELPINIT: cb, then ulReturnCode. */
enum
{
    HELPINIT_SIZE = 44,
    HELPINIT_ULRETURNCODE_OFFSET = 4
};

#include "os2/ps/helpmgr/generated/helpmgr-lx.h"

HWND WinCreateHelpInstance(HAB hab, PVOID init)
{
    TRACE_NATIVE("WinCreateHelpInstance(%u, %p)", (uint)hab, init);
    if (init && lx_guest_load_u32(init) == HELPINIT_SIZE)
        /* No information-presentation renderer is available. */
        lx_guest_store_u32((uint8 *)init + HELPINIT_ULRETURNCODE_OFFSET, HMERR_LOAD_DLL);
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
