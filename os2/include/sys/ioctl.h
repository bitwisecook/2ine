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

    /* The upper word of a command holds its direction and parameter length; the stack
       matches the group and number in the lower word, which is what the Toolkit's
       kernel _IOC form produces. */
#define OS2_IOCPARM_MASK 0x1fff

#define OS2_IOCPARM_LEN(x) (((x) >> 16) & OS2_IOCPARM_MASK)

#define OS2_IOCGROUP(x) (((x) >> 8) & 0xff)

#define OS2__IOC(inout, group, num, len) (((group) << 8) | (num))

#define OS2_FIONBIO OS2__IOC(0, 'f', 126, 0)

#define OS2_FIONREAD OS2__IOC(0, 'f', 127, 0)

#define OS2_OSIOCGIFADDR OS2__IOC(0, 'i', 13, 0)

#define OS2_OSIOCGIFDSTADDR OS2__IOC(0, 'i', 15, 0)

#define OS2_SIOCGIFFLAGS OS2__IOC(0, 'i', 17, 0)

#define OS2_OSIOCGIFBRDADDR OS2__IOC(0, 'i', 18, 0)

#define OS2_OSIOCGIFCONF OS2__IOC(0, 'i', 20, 0)

#define OS2_OSIOCGIFNETMASK OS2__IOC(0, 'i', 21, 0)

#define OS2_SIOCGIFMETRIC OS2__IOC(0, 'i', 23, 0)

#define OS2_SIOCGIFADDR OS2__IOC(0, 'i', 33, 0)

#define OS2_SIOCGIFDSTADDR OS2__IOC(0, 'i', 34, 0)

#define OS2_SIOCGIFBRDADDR OS2__IOC(0, 'i', 35, 0)

#define OS2_SIOCGIFCONF OS2__IOC(0, 'i', 36, 0)

#define OS2_SIOCGIFNETMASK OS2__IOC(0, 'i', 37, 0)

#define OS2_SIOCGIFMTU OS2__IOC(0, 'i', 57, 0)

#define OS2_SIOSTATRT OS2__IOC(0, 'n', 45, 0)

#define OS2_SIOSTATIF OS2__IOC(0, 'n', 48, 0)

#define OS2_SIOSTATIF42 OS2__IOC(0, 'n', 51, 0)

#define OS2_SIOSTATCNTRT OS2__IOC(0, 'n', 52, 0)

    OS2EXPORT int OS2API OS2_ioctl(int sock, uint32_t command, void *argument);

    OS2EXPORT int OS2API OS2_os2_ioctl(int sock, uint32_t command, void *argument, int length);

#ifdef __cplusplus
}
#endif

#endif
