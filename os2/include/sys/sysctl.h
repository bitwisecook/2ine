/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* TCP/IP Stack configuration. The OS2_ prefix keeps these apart from the host's BSD names; the guest exports use the OS/2 names. */

#ifndef _INCL_SYS_SYSCTL_H_
#define _INCL_SYS_SYSCTL_H_

#include "os2/include/os2def.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Stack configuration */

#define OS2_CTL_NET 4

    OS2EXPORT int OS2API OS2_sysctl(const int *name, unsigned count, void *output, size_t *length,
                                    const void *input, size_t input_length);

#ifdef __cplusplus
}
#endif

#endif
