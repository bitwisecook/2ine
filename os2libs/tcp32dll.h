/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_TCP32DLL_H
#define TWOINE_TCP32DLL_H
#include "os2types.h"
typedef struct Os2LegacyHost
{
    char *name;
    char **aliases;
    int32_t family, length;
    char **addresses;
} Os2LegacyHost;
OS2EXPORT uint32_t OS2API Os2LegacyLongSwap(uint32_t value) OS2APIINFO(ord = 3, name = lswap);
OS2EXPORT uint16_t OS2API Os2LegacyShortSwap(uint16_t value) OS2APIINFO(ord = 4, name = bswap);
OS2EXPORT uint32_t OS2API Os2LegacyInetAddr(const char *name) OS2APIINFO(ord = 5, name = inet_addr);
OS2EXPORT Os2LegacyHost *OS2API Os2LegacyGetHostByName(const char *name)
    OS2APIINFO(ord = 11, name = gethostbyname);
OS2EXPORT int OS2API Os2LegacyGetHostName(char *name, int length)
    OS2APIINFO(ord = 44, name = gethostname);
#endif
