/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef _INCL_OS2ME_H_
#define _INCL_OS2ME_H_
#include "os2/include/os2def.h"
#include "os2/include/mmioos2.h"

/* INCL_MCIOS2: media control interface */
OS2EXPORT ULONG OS2API mciSendCommand(USHORT device, USHORT message, ULONG flags, PVOID parameters,
                                      USHORT user);
OS2EXPORT ULONG OS2API mciSendString(PSZ command, PSZ output, USHORT length, ULONG window,
                                     USHORT user);
OS2EXPORT ULONG OS2API mciGetErrorString(ULONG error, PSZ output, USHORT length);
#endif
