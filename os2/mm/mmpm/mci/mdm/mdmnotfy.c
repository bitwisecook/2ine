/* Public domain. No rights reserved. Wholly AI-authored. */
/* MM_MCINOTIFY posting for every MMPM device. */
#include "os2/mm/mmpm/mci/mdm/mdm.h"
#include "os2/include/pmwin.h"

void lx_mci_notify(ULONG window, USHORT device, USHORT code, ULONG user)
{
    WinPostMsg(window, MM_MCINOTIFY, (MPARAM)(uintptr_t)((ULONG)device << 16 | code),
               (MPARAM)(uintptr_t)user);
}
