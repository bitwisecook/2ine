/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef _INCL_PMSPLP_H_
#define _INCL_PMSPLP_H_
#include "os2/include/pmshl.h"
#include "os2/include/pmspl.h"
#include "os2/include/private/pmdevp.h"

/* HINI_PROFILE supplies the same USER/SYSTEM merge as native spooler clients.
   An explicitly empty or unknown queue is not a request for the PDF fallback. */
static inline int lx_printer_default_queue(void)
{
    char name[256] = {0};
    if (!PrfQueryProfileString(HINI_PROFILE, "PM_SPOOLER", "QUEUE", NULL, name, sizeof name))
        return -1;
    char *separator = strchr(name, ';');
    if (separator)
        *separator = 0;
    for (unsigned i = 0; i < 2; i++)
        if (!strcasecmp(name, lx_printer_queues[i]))
            return (int)i;
    return -1;
}
#endif
