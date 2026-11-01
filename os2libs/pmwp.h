/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_OS2LIBS_PMWP_H
#define TWOINE_OS2LIBS_PMWP_H
#include "os2types.h"
OS2EXPORT BOOL OS2API WinSetObjectData(ULONG object, PSZ settings) OS2APIINFO(250);
OS2EXPORT BOOL OS2API WinIsSomDDReady(void) OS2APIINFO(480);
OS2EXPORT APIRET OS2API WinRestartSomDD(BOOL state) OS2APIINFO(464);
#endif
