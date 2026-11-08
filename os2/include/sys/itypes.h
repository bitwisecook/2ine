/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* TCP/IP Byte order. The OS2_ prefix keeps these apart from the host's BSD names; the guest exports use the OS/2 names. */

#ifndef _INCL_SYS_ITYPES_H_
#define _INCL_SYS_ITYPES_H_

#include "os2/include/os2def.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Byte order */

    OS2EXPORT uint32_t OS2API OS2_htonl(uint32_t value);

    OS2EXPORT uint32_t OS2API OS2_ntohl(uint32_t value);

    OS2EXPORT uint16_t OS2API OS2_ntohs(uint16_t value);

    OS2EXPORT unsigned short OS2API OS2_htons(unsigned short val);

#ifdef __cplusplus
}
#endif

#endif
