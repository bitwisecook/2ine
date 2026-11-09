/* Public domain. No rights reserved. Wholly AI-authored. */
/* MDM termination: closes every MCI device when MMPM unloads. */
#include "os2/mm/mmpm/mci/mdm/mdm.h"
#include "os2/mm/mmpm/mci/mdm/mdm_video.h"

void lx_mdm_terminate(void)
{
    lx_mci_video_shutdown();
    lx_mci_devices_terminate();
}
