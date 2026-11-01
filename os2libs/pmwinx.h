/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 exports use OS/2's caller-cleaned 32-bit system linkage. */
#ifndef TWOINE_OS2LIBS_PMWINX_H
#define TWOINE_OS2LIBS_PMWINX_H
#include "os2types.h"
#ifdef __cplusplus
extern "C"
{
#endif
    OS2EXPORT ULONG OS2API LoadCursor(ULONG instance, ULONG name)
        OS2APIINFO(ord = 459, name = LOADCURSOR);
    OS2EXPORT ULONG OS2API SetCursor(ULONG cursor) OS2APIINFO(ord = 605, name = SETCURSOR);
    OS2EXPORT BOOL OS2API DestroyCursor(ULONG cursor) OS2APIINFO(ord = 143, name = DESTROYCURSOR);
    OS2EXPORT VOID OS2API WinInitializeOpen32(ULONG reserved)
        OS2APIINFO(ord = 6009, name = WININITIALIZEOPEN32);
    OS2EXPORT VOID OS2API WinTerminateOpen32(ULONG reserved)
        OS2APIINFO(ord = 6010, name = WINTERMINATEOPEN32);
    OS2EXPORT BOOL OS2API WinXProcessMsg(PVOID message)
        OS2APIINFO(ord = 6007, name = WINXPROCESSMSG);
    OS2EXPORT LONG OS2API MulDiv(LONG number, LONG numerator, LONG denominator)
        OS2APIINFO(ord = 494, name = MULDIV);
    OS2EXPORT LONG OS2API LoadStringOpen32(ULONG module, ULONG id, PCHAR output, LONG capacity)
        OS2APIINFO(ord = 464, name = LOADSTRING);
    OS2EXPORT ULONG OS2API GetDCOpen32(ULONG window) OS2APIINFO(ord = 263, name = GETDC);
    OS2EXPORT LONG OS2API GetDeviceCapsOpen32(ULONG dc, LONG index)
        OS2APIINFO(ord = 269, name = GETDEVICECAPS);
    OS2EXPORT LONG OS2API ReleaseDCOpen32(ULONG window, ULONG dc)
        OS2APIINFO(ord = 560, name = RELEASEDC);
    OS2EXPORT ULONG OS2API LoadLibraryOpen32(PSZ name) OS2APIINFO(ord = 461, name = LOADLIBRARY);
    OS2EXPORT BOOL OS2API FreeLibraryOpen32(ULONG module) OS2APIINFO(ord = 223, name = FREELIBRARY);
    OS2EXPORT LONG OS2API RegCloseKey(ULONG key) OS2APIINFO(ord = 541, name = REGCLOSEKEY);
    OS2EXPORT LONG OS2API RegOpenKey(ULONG key, PSZ name, PULONG output)
        OS2APIINFO(ord = 549, name = REGOPENKEY);
    OS2EXPORT LONG OS2API RegOpenKeyEx(ULONG key, PSZ name, ULONG options, ULONG access,
                                       PULONG output) OS2APIINFO(ord = 550, name = REGOPENKEYEX);
    OS2EXPORT LONG OS2API RegCreateKey(ULONG key, PSZ name, PULONG output)
        OS2APIINFO(ord = 542, name = REGCREATEKEY);
    OS2EXPORT LONG OS2API RegCreateKeyEx(ULONG key, PSZ name, ULONG reserved, PSZ class_name,
                                         ULONG options, ULONG access, PVOID security, PULONG output,
                                         PULONG disposition)
        OS2APIINFO(ord = 543, name = REGCREATEKEYEX);
    OS2EXPORT LONG OS2API RegQueryValueEx(ULONG key, PSZ name, PULONG reserved, PULONG type,
                                          PVOID data, PULONG size)
        OS2APIINFO(ord = 553, name = REGQUERYVALUEEX);
    OS2EXPORT LONG OS2API RegEnumValue(ULONG key, ULONG index, PSZ name, PULONG name_size,
                                       PULONG reserved, PULONG type, PVOID data, PULONG data_size)
        OS2APIINFO(ord = 548, name = REGENUMVALUE);
    OS2EXPORT LONG OS2API RegDeleteValue(ULONG key, PSZ name)
        OS2APIINFO(ord = 545, name = REGDELETEVALUE);
    OS2EXPORT LONG OS2API RegSetValueEx(ULONG key, PSZ name, ULONG reserved, ULONG type, PVOID data,
                                        ULONG size) OS2APIINFO(ord = 555, name = REGSETVALUEEX);
#ifdef __cplusplus
}
#endif
#endif
