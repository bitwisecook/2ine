/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private MMPM/2 interfaces: MMIO services used by the MCI devices and the tests. */
#ifndef _INCL_OS2MEP_H_
#define _INCL_OS2MEP_H_
#include "os2/include/mmioos2.h"
#include <stddef.h>
/* Returns an owned copy of a memory-backed media element. */
OS2EXPORT unsigned char *lx_mmio_copy(ULONG handle, size_t *size);
#endif
