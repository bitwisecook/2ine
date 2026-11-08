/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* TCP/IP Socket control. The OS2_ prefix keeps these apart from the host's BSD names; the guest exports use the OS/2 names. */

#ifndef _INCL_SYS_IOCTL_H_
#define _INCL_SYS_IOCTL_H_

#include "os2/include/os2def.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Socket control */

    OS2EXPORT int OS2API OS2_ioctl(int sock, uint32_t command, void *argument);

    OS2EXPORT int OS2API OS2_os2_ioctl(int sock, uint32_t command, void *argument, int length);

#ifdef __cplusplus
}
#endif

#endif
