/* Public domain. No rights reserved. Wholly AI-authored. */
/* Persistent ball-local Workplace Shell folder and program objects. */
#ifndef TWOINE_PMWP_H
#define TWOINE_PMWP_H
#include "os2/include/os2types.h"
#include "os2/include/pmwin.h"
OS2EXPORT BOOL OS2API WinRegisterObjectClass(PSZ name, PSZ module);
OS2EXPORT BOOL OS2API WinDeregisterObjectClass(PSZ name);
OS2EXPORT BOOL OS2API WinSetObjectData(ULONG object, PSZ setup);
OS2EXPORT BOOL OS2API WinDestroyObject(ULONG object);
OS2EXPORT ULONG OS2API WinQueryObject(PSZ id);
OS2EXPORT ULONG OS2API WinCreateObject(PSZ class_name, PSZ title, PSZ setup, PSZ location,
                                       ULONG flags);
OS2EXPORT BOOL OS2API WinIsSomDDReady(void);
OS2EXPORT APIRET OS2API WinRestartSomDD(BOOL state);

OS2EXPORT BOOL OS2API WinEnumObjectClasses(PVOID a0, PULONG a1);
OS2EXPORT BOOL OS2API WinStoreWindowPos(PSZ a0, PSZ a1, HWND a2);
OS2EXPORT BOOL OS2API WinRestoreWindowPos(PSZ a0, PSZ a1, HWND a2);
OS2EXPORT HPOINTER OS2API WinLoadFileIcon(PSZ a0, BOOL a1);
OS2EXPORT BOOL OS2API WinFreeFileIcon(HPOINTER a0);
OS2EXPORT BOOL OS2API WinReplaceObjectClass(PSZ a0, PSZ a1, BOOL a2);
OS2EXPORT BOOL OS2API WinQueryActiveDesktopPathname(PSZ a0, ULONG a1);
OS2EXPORT BOOL OS2API WinQueryObjectPath(ULONG a0, PSZ a1, ULONG a2);
OS2EXPORT BOOL OS2API WinSaveObject(ULONG a0, BOOL a1);
OS2EXPORT BOOL OS2API WinOpenObject(ULONG a0, ULONG a1, BOOL a2);
OS2EXPORT ULONG OS2API WinMoveObject(ULONG a0, ULONG a1, ULONG a2);
OS2EXPORT ULONG OS2API WinCopyObject(ULONG a0, ULONG a1, ULONG a2);
OS2EXPORT ULONG OS2API WinCreateShadow(ULONG a0, ULONG a1, ULONG a2);
OS2EXPORT APIRET OS2API WinRestartWPDServer(BOOL a0);
OS2EXPORT BOOL OS2API WinIsWPDServerReady(void);

/* Class constructors return the guest SOM class object, or zero on failure. */
OS2EXPORT ULONG OS2API WPFilesystemNewClass(ULONG major, ULONG minor);
OS2EXPORT ULONG OS2API WPFolderNewClass(ULONG major, ULONG minor);
OS2EXPORT ULONG OS2API WPDataFileNewClass(ULONG major, ULONG minor);
OS2EXPORT ULONG OS2API WPObjectNewClass(ULONG major, ULONG minor);
OS2EXPORT ULONG OS2API WPAbstractNewClass(ULONG major, ULONG minor);
OS2EXPORT ULONG OS2API WPTransientNewClass(ULONG major, ULONG minor);
OS2EXPORT ULONG OS2API WPDesktopNewClass(ULONG major, ULONG minor);
OS2EXPORT ULONG OS2API WPClassManagerNewClass(ULONG major, ULONG minor);
OS2EXPORT ULONG OS2API M_WPFolderNewClass(ULONG major, ULONG minor);
OS2EXPORT ULONG OS2API M_WPDataFileNewClass(ULONG major, ULONG minor);

/* Mutable guest data exports: classObject followed by SOM method token words. The
   generated module glue maps them; guest SOM fills them when a class is built.
   pmwp.def gives each export's ordinal; lxapigen.py reads the size here. */
#define LX_GUEST_DATA_EXPORT(name, bytes)                                                          \
    enum                                                                                           \
    {                                                                                              \
        name##_bytes = bytes                                                                       \
    }
LX_GUEST_DATA_EXPORT(WPFilesystemClassData, 176);
LX_GUEST_DATA_EXPORT(WPFolderClassData, 436);
LX_GUEST_DATA_EXPORT(WPObjectClassData, 664);
LX_GUEST_DATA_EXPORT(WPDiskClassData, 48);
LX_GUEST_DATA_EXPORT(WPProgramClassData, 168);
LX_GUEST_DATA_EXPORT(M_WPFileSystemClassData, 28);
LX_GUEST_DATA_EXPORT(M_WPObjectClassData, 184);
/* Starts the first program object whose ASSOCFILTER matches the file name. */
BOOL lx_pmwp_open_associated(PSZ path);
#endif
