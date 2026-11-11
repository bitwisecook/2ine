/* Public domain. No rights reserved. Wholly AI-authored. */
/* Hosts without a native dialog helper; callers use their own presenter. */
#include "host/dialog.h"

LxHostSaveResult lx_host_dialog_run(const char *const arguments[], char **output)
{
    (void)arguments;
    if (output)
        *output = NULL;
    return LX_HOST_SAVE_ERROR;
}
