/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_MMIO_H
#define TWOINE_MMIO_H
#include "os2types.h"
#include <stddef.h>
OS2EXPORT ULONG OS2API mmioOpen(PSZ name, PVOID info, ULONG flags) OS2APIINFO(ord = 27);
OS2EXPORT USHORT OS2API mmioClose(ULONG handle, USHORT flags) OS2APIINFO(ord = 32);
/* Returns an owned copy of a memory-backed media element. */
OS2EXPORT unsigned char *lx_mmio_copy(ULONG handle, size_t *size);
#endif
