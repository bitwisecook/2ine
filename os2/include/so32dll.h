/* Public domain. No rights reserved. Wholly AI-authored. */
/* TCP/IP 4.0 sockets use a 16-bit family field and the original SO32DLL ordinals. */
#ifndef TWOINE_SO32DLL_H
#define TWOINE_SO32DLL_H
#include "os2/include/os2def.h"
OS2EXPORT uint32_t OS2API Os2LegacyGetHostId(void);

typedef struct Os2LegacyAddress
{
    uint16_t family, port;
    uint32_t address;
    uint8_t padding[8];
} Os2LegacyAddress;

OS2EXPORT int OS2API Os2LegacyAccept(int socket, Os2LegacyAddress *address, int *length);
OS2EXPORT int OS2API Os2LegacyConnect(int socket, const Os2LegacyAddress *address, int length);
OS2EXPORT int OS2API Os2LegacyListen(int socket, int backlog);
OS2EXPORT int OS2API Os2LegacyRecv(int socket, void *buffer, int length, int flags);
OS2EXPORT int OS2API Os2LegacySend(int socket, const void *buffer, int length, int flags);
OS2EXPORT int OS2API Os2LegacyShutdown(int socket, int how);
OS2EXPORT int OS2API Os2LegacyBind(int socket, const Os2LegacyAddress *address, int length);
OS2EXPORT int OS2API Os2LegacyGetSocketName(int socket, Os2LegacyAddress *address, int *length);
OS2EXPORT int OS2API Os2LegacySocketOption(int socket, int level, int option, const void *value,
                                           int length);
OS2EXPORT int OS2API Os2LegacyIoctl(int socket, int command, void *argument, int length);
OS2EXPORT int OS2API Os2LegacyRecvFrom(int socket, void *buffer, int length, int flags,
                                       Os2LegacyAddress *address, int *address_length);
OS2EXPORT int OS2API Os2LegacySendTo(int socket, const void *buffer, int length, int flags,
                                     const Os2LegacyAddress *address, int address_length);
OS2EXPORT int OS2API Os2LegacySocket(int family, int type, int protocol);
OS2EXPORT int OS2API Os2LegacySocketClose(int socket);
OS2EXPORT int OS2API Os2LegacySocketError(void);
OS2EXPORT int OS2API Os2LegacySocketInit(void);
#endif
