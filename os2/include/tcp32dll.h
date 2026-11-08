/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_TCP32DLL_H
#define TWOINE_TCP32DLL_H
#include "os2/include/os2def.h"

typedef struct Os2LegacyHost
{
    char *name;
    char **aliases;
    int32_t family, length;
    char **addresses;
} Os2LegacyHost;

typedef struct Os2LegacyProtocol
{
    char *name;
    char **aliases;
    int number;
} Os2LegacyProtocol;

typedef struct Os2LegacyService
{
    char *name;
    char **aliases;
    int port;
    char *protocol;
} Os2LegacyService;

OS2EXPORT Os2LegacyHost *OS2API Os2LegacyGetHostByAddress(const void *address, int length,
                                                          int family);
OS2EXPORT Os2LegacyProtocol *OS2API Os2LegacyGetProtocolByName(const char *name);
OS2EXPORT Os2LegacyProtocol *OS2API Os2LegacyGetProtocolByNumber(int number);
OS2EXPORT Os2LegacyService *OS2API Os2LegacyGetServiceByPort(int port, const char *protocol);
OS2EXPORT Os2LegacyService *OS2API Os2LegacyGetServiceByName(const char *name,
                                                             const char *protocol);
OS2EXPORT int *OS2API Os2LegacyResolverError(void);
OS2EXPORT uint32_t OS2API Os2LegacyLongSwap(uint32_t value);
OS2EXPORT uint16_t OS2API Os2LegacyShortSwap(uint16_t value);
OS2EXPORT uint32_t OS2API Os2LegacyInetAddr(const char *name);
OS2EXPORT char *OS2API Os2LegacyInetText(uint32_t address);
OS2EXPORT Os2LegacyHost *OS2API Os2LegacyGetHostByName(const char *name);
OS2EXPORT int OS2API Os2LegacyGetHostName(char *name, int length);
#endif
