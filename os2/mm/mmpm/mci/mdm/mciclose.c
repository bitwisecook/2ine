/* Public domain. No rights reserved. Wholly AI-authored. */
/* MCI_STOP and MCI_CLOSE for the waveaudio and sequencer devices. */
#include "os2/mm/mmpm/mci/mdm/mdm.h"
#include "os2/mm/mmpm/mci/admc/admc.h"
#include "os2/mm/mmpm/mci/mimc/mimc.h"

void lx_mci_stop(LxMciDevice *d, int closing)
{
    lx_host_monitor_lock(lx_mci_monitor);
    d->stopped = 1;
    d->ending = 0;
    lx_host_monitor_wake_all(lx_mci_monitor);
    lx_host_monitor_unlock(lx_mci_monitor);
    if (d->thread)
    {
        lx_host_thread_join(d->thread, NULL);
        d->thread = NULL;
    }
    lx_host_monitor_lock(lx_mci_monitor);
    lx_admc_discard(d, closing);
    if (closing)
    {
        lx_mimc_close(d);
        memset(d, 0, sizeof *d);
    }
    lx_host_monitor_unlock(lx_mci_monitor);
}
