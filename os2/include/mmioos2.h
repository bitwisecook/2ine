/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef _INCL_MMIOOS2_H_
#define _INCL_MMIOOS2_H_
#include "os2/include/os2def.h"
#include <stddef.h>

/* mmioOpen flags */
enum
{
    MMIO_CREATE = 0x00000001,
    MMIO_READ = 0x00000004,
    MMIO_WRITE = 0x00000008,
    MMIO_READWRITE = 0x00000010
};

/* mmioInstallIOProc flags */
enum
{
    MMIO_INSTALLPROC = 0x00000001,
    MMIO_REMOVEPROC = 0x00000002
};

/* IOProc messages */
enum
{
    MMIOM_START = 0x0E00,
    MMIOM_CLOSE = MMIOM_START + 3,
    MMIOM_OPEN = MMIOM_START + 4,
    MMIOM_READ = MMIOM_START + 5,
    MMIOM_SEEK = MMIOM_START + 6,
    MMIOM_WRITE = MMIOM_START + 7,
    MMIOM_GETHEADER = MMIOM_START + 9
};

enum
{
    MMIO_SUCCESS = 0,
    MMIO_ERROR = 0xFFFFFFFF,
    MMIOERR_UNSUPPORTED_MESSAGE = 0xFFFFFFFE
};

#define mmioFOURCC(ch0, ch1, ch2, ch3)                                                             \
    ((ULONG)(BYTE)(ch0) | ((ULONG)(BYTE)(ch1) << 8) | ((ULONG)(BYTE)(ch2) << 16) |                 \
     ((ULONG)(BYTE)(ch3) << 24))

enum
{
    FOURCC_MEM = mmioFOURCC('M', 'E', 'M', ' '),
    FOURCC_DOS = mmioFOURCC('D', 'O', 'S', ' ')
};

OS2EXPORT ULONG OS2API mmioOpen(PSZ name, PVOID info, ULONG flags);
OS2EXPORT USHORT OS2API mmioClose(ULONG handle, USHORT flags);
OS2EXPORT ULONG OS2API mmioInstallIOProc(ULONG fourcc, ULONG procedure, ULONG flags);
OS2EXPORT LONG OS2API mmioRead(ULONG handle, PCHAR buffer, LONG bytes);
OS2EXPORT LONG OS2API mmioWrite(ULONG handle, PCHAR buffer, LONG bytes);
OS2EXPORT LONG OS2API mmioSeek(ULONG handle, LONG offset, LONG origin);
OS2EXPORT ULONG OS2API mmioGetHeader(ULONG handle, PVOID header, LONG capacity, PLONG bytes,
                                     ULONG reserved, ULONG flags);
#endif
