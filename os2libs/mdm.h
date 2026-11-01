/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_MDM_H
#define TWOINE_MDM_H
#include "os2types.h"
OS2EXPORT ULONG OS2API mciSendCommand(USHORT device, USHORT message, ULONG flags, PVOID parameters,
                                      USHORT user) OS2APIINFO(ord = 1);
OS2EXPORT ULONG OS2API mciSendString(PSZ command, PSZ output, USHORT length, ULONG window,
                                     USHORT user) OS2APIINFO(ord = 2);
OS2EXPORT ULONG OS2API mciGetErrorString(ULONG error, PSZ output, USHORT length)
    OS2APIINFO(ord = 3);
#endif
