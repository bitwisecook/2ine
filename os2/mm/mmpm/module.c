/* Public domain. No rights reserved. Wholly AI-authored. */
/* MMPM entry: the MCI units live under mci/ and multimedia file I/O under mmio/. */
#include "os2/mm/mmpm/mci/mdm/mdm.h"
#include "os2/mm/mmpm/mci/admc/admc.h"

#define LX_NATIVE_MODULE_SHUTDOWN() lx_mdm_terminate()
#include "os2/mm/mmpm/generated/mmpm-lx.h"
