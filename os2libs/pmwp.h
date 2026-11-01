/* Public domain. No rights reserved. Wholly AI-authored. */
/* Persistent bottle-local Workplace Shell folder and program objects. */
#ifndef TWOINE_PMWP_H
#define TWOINE_PMWP_H
#include "os2types.h"
OS2EXPORT BOOL OS2API WinRegisterObjectClass(PSZ name, PSZ module) OS2APIINFO(200);
OS2EXPORT BOOL OS2API WinDeregisterObjectClass(PSZ name) OS2APIINFO(201);
OS2EXPORT BOOL OS2API WinSetObjectData(ULONG object, PSZ setup) OS2APIINFO(250);
OS2EXPORT BOOL OS2API WinDestroyObject(ULONG object) OS2APIINFO(251);
OS2EXPORT ULONG OS2API WinQueryObject(PSZ id) OS2APIINFO(252);
OS2EXPORT ULONG OS2API WinCreateObject(PSZ class_name, PSZ title, PSZ setup, PSZ location,
                                       ULONG flags) OS2APIINFO(281);
OS2EXPORT BOOL OS2API WinIsSomDDReady(void) OS2APIINFO(480);
OS2EXPORT APIRET OS2API WinRestartSomDD(BOOL state) OS2APIINFO(464);
#endif
