/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_OS2LIBS_PMDRAG_H
#define TWOINE_OS2LIBS_PMDRAG_H
#include "os2types.h"
OS2EXPORT ULONG OS2API DrgAddStrHandle(PSZ name) OS2APIINFO(33);
OS2EXPORT BOOL OS2API DrgDeleteStrHandle(ULONG string) OS2APIINFO(37);
OS2EXPORT ULONG OS2API DrgQueryStrName(ULONG string, ULONG capacity, PSZ output) OS2APIINFO(49);
OS2EXPORT ULONG OS2API DrgQueryStrNameLen(ULONG string) OS2APIINFO(50);
#endif
