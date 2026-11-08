/* Public domain. No rights reserved. Wholly AI-authored. */
/* Print destinations own their path and printer name. */
#include "host/print.h"
#include <stdlib.h>
#include <string.h>

void lx_host_print_destination_clear(LxHostPrintDestination *destination)
{
    if (destination)
    {
        free(destination->path);
        free(destination->printer);
        memset(destination, 0, sizeof *destination);
    }
}
