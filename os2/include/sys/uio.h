/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* TCP/IP Scatter/gather output. The OS2_ prefix keeps these apart from the host's BSD names; the guest exports use the OS/2 names. */

#ifndef _INCL_SYS_UIO_H_
#define _INCL_SYS_UIO_H_

#include "os2/include/os2def.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Scatter/gather output */

#pragma pack(push, 1)

    typedef struct OS2_iovec
    {
        void *buffer;
        size_t length;
    } OS2_iovec;

#pragma pack(pop)

    OS2EXPORT int OS2API OS2_writev(int sock, const OS2_iovec *vectors, int count);

#ifdef __cplusplus
}
#endif

#endif
