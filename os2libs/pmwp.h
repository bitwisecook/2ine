/* Public domain. No rights reserved. Wholly AI-authored. */
/* Persistent ball-local Workplace Shell folder and program objects. */
#ifndef TWOINE_PMWP_H
#define TWOINE_PMWP_H
#include "os2types.h"
#include "os2libs/pmwin.h"
OS2EXPORT BOOL OS2API WinRegisterObjectClass(PSZ name, PSZ module) OS2APIINFO(200);
OS2EXPORT BOOL OS2API WinDeregisterObjectClass(PSZ name) OS2APIINFO(201);
OS2EXPORT BOOL OS2API WinSetObjectData(ULONG object, PSZ setup) OS2APIINFO(250);
OS2EXPORT BOOL OS2API WinDestroyObject(ULONG object) OS2APIINFO(251);
OS2EXPORT ULONG OS2API WinQueryObject(PSZ id) OS2APIINFO(252);
OS2EXPORT ULONG OS2API WinCreateObject(PSZ class_name, PSZ title, PSZ setup, PSZ location,
                                       ULONG flags) OS2APIINFO(281);
OS2EXPORT BOOL OS2API WinIsSomDDReady(void) OS2APIINFO(480);
OS2EXPORT APIRET OS2API WinRestartSomDD(BOOL state) OS2APIINFO(464);

OS2EXPORT BOOL OS2API WinEnumObjectClasses(PVOID a0, PULONG a1) OS2APIINFO(205);
OS2EXPORT BOOL OS2API WinStoreWindowPos(PSZ a0, PSZ a1, HWND a2) OS2APIINFO(207);
OS2EXPORT BOOL OS2API WinRestoreWindowPos(PSZ a0, PSZ a1, HWND a2) OS2APIINFO(208);
OS2EXPORT HPOINTER OS2API WinLoadFileIcon(PSZ a0, BOOL a1) OS2APIINFO(209);
OS2EXPORT BOOL OS2API WinFreeFileIcon(HPOINTER a0) OS2APIINFO(216);
OS2EXPORT BOOL OS2API WinReplaceObjectClass(PSZ a0, PSZ a1, BOOL a2) OS2APIINFO(219);
OS2EXPORT BOOL OS2API WinQueryActiveDesktopPathname(PSZ a0, ULONG a1) OS2APIINFO(262);
OS2EXPORT BOOL OS2API WinQueryObjectPath(ULONG a0, PSZ a1, ULONG a2) OS2APIINFO(263);
OS2EXPORT BOOL OS2API WinSaveObject(ULONG a0, BOOL a1) OS2APIINFO(285);
OS2EXPORT BOOL OS2API WinOpenObject(ULONG a0, ULONG a1, BOOL a2) OS2APIINFO(286);
OS2EXPORT ULONG OS2API WinMoveObject(ULONG a0, ULONG a1, ULONG a2) OS2APIINFO(287);
OS2EXPORT ULONG OS2API WinCopyObject(ULONG a0, ULONG a1, ULONG a2) OS2APIINFO(288);
OS2EXPORT ULONG OS2API WinCreateShadow(ULONG a0, ULONG a1, ULONG a2) OS2APIINFO(289);
OS2EXPORT APIRET OS2API WinRestartWPDServer(BOOL a0) OS2APIINFO(463);
OS2EXPORT BOOL OS2API WinIsWPDServerReady(void) OS2APIINFO(465);

/* Class constructors return the guest SOM class object, or zero on failure. */
OS2EXPORT ULONG OS2API WPFilesystemNewClass(ULONG major, ULONG minor) OS2APIINFO(12);
OS2EXPORT ULONG OS2API WPFolderNewClass(ULONG major, ULONG minor) OS2APIINFO(15);
OS2EXPORT ULONG OS2API WPDataFileNewClass(ULONG major, ULONG minor) OS2APIINFO(18);
OS2EXPORT ULONG OS2API WPObjectNewClass(ULONG major, ULONG minor) OS2APIINFO(24);
OS2EXPORT ULONG OS2API WPAbstractNewClass(ULONG major, ULONG minor) OS2APIINFO(27);
OS2EXPORT ULONG OS2API WPTransientNewClass(ULONG major, ULONG minor) OS2APIINFO(42);
OS2EXPORT ULONG OS2API WPDesktopNewClass(ULONG major, ULONG minor) OS2APIINFO(57);
OS2EXPORT ULONG OS2API WPClassManagerNewClass(ULONG major, ULONG minor) OS2APIINFO(90);
OS2EXPORT ULONG OS2API M_WPFolderNewClass(ULONG major, ULONG minor) OS2APIINFO(315);
OS2EXPORT ULONG OS2API M_WPDataFileNewClass(ULONG major, ULONG minor) OS2APIINFO(318);

/* Mutable guest data exports: classObject followed by SOM method token words. The
   generated module glue maps them; guest SOM fills them when a class is built. */
#define LX_GUEST_DATA_EXPORT(name, ordinal, bytes)                                                 \
    enum                                                                                           \
    {                                                                                              \
        name##_ordinal = ordinal,                                                                  \
        name##_bytes = bytes                                                                       \
    }
LX_GUEST_DATA_EXPORT(WPFilesystemClassData, 11, 176);
LX_GUEST_DATA_EXPORT(WPFolderClassData, 14, 436);
LX_GUEST_DATA_EXPORT(WPObjectClassData, 23, 664);
LX_GUEST_DATA_EXPORT(WPDiskClassData, 35, 48);
LX_GUEST_DATA_EXPORT(WPProgramClassData, 47, 168);
LX_GUEST_DATA_EXPORT(M_WPFileSystemClassData, 311, 28);
LX_GUEST_DATA_EXPORT(M_WPObjectClassData, 323, 184);
/* Starts the first program object whose ASSOCFILTER matches the file name. */
BOOL lx_pmwp_open_associated(PSZ path);
#endif
