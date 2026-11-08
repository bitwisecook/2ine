/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_MMIO_H
#define TWOINE_MMIO_H
#include "os2/include/os2types.h"
#include <stddef.h>
OS2EXPORT ULONG OS2API mmioOpen(PSZ name, PVOID info, ULONG flags);
OS2EXPORT USHORT OS2API mmioClose(ULONG handle, USHORT flags);
OS2EXPORT ULONG OS2API mmioInstallIOProc(ULONG fourcc, ULONG procedure, ULONG flags);
OS2EXPORT LONG OS2API mmioRead(ULONG handle, PCHAR buffer, LONG bytes);
OS2EXPORT LONG OS2API mmioWrite(ULONG handle, PCHAR buffer, LONG bytes);
OS2EXPORT LONG OS2API mmioSeek(ULONG handle, LONG offset, LONG origin);
OS2EXPORT ULONG OS2API mmioGetHeader(ULONG handle, PVOID header, LONG capacity, PLONG bytes,
                                     ULONG reserved, ULONG flags);
/* Returns an owned copy of a memory-backed media element. */
OS2EXPORT unsigned char *lx_mmio_copy(ULONG handle, size_t *size);
#endif
