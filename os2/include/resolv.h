/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* TCP/IP Resolver. The OS2_ prefix keeps these apart from the host's BSD names; the guest exports use the OS/2 names. */

#ifndef _INCL_RESOLV_H_
#define _INCL_RESOLV_H_

#include "os2/include/os2def.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Resolver */

    OS2EXPORT int OS2API OS2_res_init(void);

#ifdef __cplusplus
}
#endif

#endif
