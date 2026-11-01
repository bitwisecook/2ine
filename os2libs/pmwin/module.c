/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/pmwin/internal.h"
#include "os2libs/generated/pmwin-lx.h"

LX_PRIVATE uint32 GHABCounter;
LX_PRIVATE uint32 GHMQCounter;

LX_PRIVATE Window desktop_window;

PFNWP defaultGuestWindowProc(void)
{
#if TWOINE_PORTABLE_LOADER && LX_LEGACY
    if (GLoaderState.execution && obj32.mapping && bridge_address_WinDefWindowProc)
        return (PFNWP)(uintptr_t)(obj32.guest_base +
                                  (uint32)((uint8 *)bridge_address_WinDefWindowProc -
                                           (uint8 *)obj32.addr));
#endif
    return WinDefWindowProc;
}
PFNWP defaultGuestDialogProc(void)
{
#if TWOINE_PORTABLE_LOADER && LX_LEGACY
    if (GLoaderState.execution && obj32.mapping && bridge_address_WinDefDlgProc)
        return (PFNWP)(uintptr_t)(obj32.guest_base +
                                  (uint32)((uint8 *)bridge_address_WinDefDlgProc -
                                           (uint8 *)obj32.addr));
#endif
    return WinDefDlgProc;
}
