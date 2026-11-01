/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 exports use OS/2's caller-cleaned 32-bit system linkage. */
#ifndef TWOINE_OS2LIBS_PMWINX_H
#define TWOINE_OS2LIBS_PMWINX_H
#include "os2types.h"
#ifdef __cplusplus
extern "C"
{
#endif
    OS2EXPORT VOID OS2API WinInitializeOpen32(ULONG reserved)
        OS2APIINFO(ord = 6009, name = WININITIALIZEOPEN32);
    OS2EXPORT VOID OS2API WinTerminateOpen32(ULONG reserved)
        OS2APIINFO(ord = 6010, name = WINTERMINATEOPEN32);
    OS2EXPORT LONG OS2API MulDiv(LONG number, LONG numerator, LONG denominator)
        OS2APIINFO(ord = 494, name = MULDIV);
    OS2EXPORT LONG OS2API RegCloseKey(ULONG key) OS2APIINFO(ord = 541, name = REGCLOSEKEY);
    OS2EXPORT LONG OS2API RegOpenKeyEx(ULONG key, PSZ name, ULONG options, ULONG access,
                                       PULONG output) OS2APIINFO(ord = 550, name = REGOPENKEYEX);
    OS2EXPORT LONG OS2API RegCreateKeyEx(ULONG key, PSZ name, ULONG reserved, PSZ class_name,
                                         ULONG options, ULONG access, PVOID security, PULONG output,
                                         PULONG disposition)
        OS2APIINFO(ord = 543, name = REGCREATEKEYEX);
    OS2EXPORT LONG OS2API RegQueryValueEx(ULONG key, PSZ name, PULONG reserved, PULONG type,
                                          PVOID data, PULONG size)
        OS2APIINFO(ord = 553, name = REGQUERYVALUEEX);
    OS2EXPORT LONG OS2API RegSetValueEx(ULONG key, PSZ name, ULONG reserved, ULONG type, PVOID data,
                                        ULONG size) OS2APIINFO(ord = 555, name = REGSETVALUEEX);
#ifdef __cplusplus
}
#endif
#endif
