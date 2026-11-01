/* Public domain. No rights reserved. Wholly AI-authored. */
/* TCP/IP 4.0 sockets use a 16-bit family field and the original SO32DLL ordinals. */
#ifndef TWOINE_SO32DLL_H
#define TWOINE_SO32DLL_H
#include "os2types.h"
typedef struct Os2LegacyAddress
{
    uint16_t family, port;
    uint32_t address;
    uint8_t padding[8];
} Os2LegacyAddress;
OS2EXPORT int OS2API Os2LegacyBind(int socket, const Os2LegacyAddress *address, int length)
    OS2APIINFO(ord = 2, name = bind);
OS2EXPORT int OS2API Os2LegacyIoctl(int socket, int command, void *argument, int length)
    OS2APIINFO(ord = 8, name = ioctl);
OS2EXPORT int OS2API Os2LegacyRecvFrom(int socket, void *buffer, int length, int flags,
                                       Os2LegacyAddress *address, int *address_length)
    OS2APIINFO(ord = 11, name = recvfrom);
OS2EXPORT int OS2API Os2LegacySendTo(int socket, const void *buffer, int length, int flags,
                                     const Os2LegacyAddress *address, int address_length)
    OS2APIINFO(ord = 14, name = sendto);
OS2EXPORT int OS2API Os2LegacySocket(int family, int type, int protocol)
    OS2APIINFO(ord = 16, name = socket);
OS2EXPORT int OS2API Os2LegacySocketClose(int socket) OS2APIINFO(ord = 17, name = soclose);
OS2EXPORT int OS2API Os2LegacySocketError(void) OS2APIINFO(ord = 20, name = sock_errno);
OS2EXPORT int OS2API Os2LegacySocketInit(void) OS2APIINFO(ord = 26, name = sock_init);
#endif
