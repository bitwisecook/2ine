/* Public domain. No rights reserved. Wholly AI-authored. */
/* Spooler catalogue queries shared by PMSPL's entry points and guest bindings.
   base is the guest address of output, used for the records' internal pointers. */
#ifndef TWOINE_PMSPL_INTERNAL_H
#define TWOINE_PMSPL_INTERNAL_H
#include "os2/include/private/export.h"
#include "os2/include/pmspl.h"
ULONG lx_spl_catalogue(const char *queue, ULONG level, PVOID output, ULONG capacity, uint32 base,
                       PULONG returned, PULONG total, PULONG needed);

ULONG lx_spl_device(const char *name, ULONG level, PVOID output, ULONG capacity, uint32 base,
                    PULONG needed);

ULONG lx_spl_printers(ULONG level, ULONG types, PVOID output, ULONG capacity, uint32 base,
                      PULONG returned, PULONG total, PULONG needed);
#endif
