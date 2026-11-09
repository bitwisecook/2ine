/* Public domain. No rights reserved. Wholly AI-authored. */
/* Shared by the colour and font common dialogs: COMMDLG error and flag values, the Windows
   messages a hook receives, and the hook and template helpers. */
#ifndef TWOINE_PMWINX_COMMON_DIALOGS_H
#define TWOINE_PMWINX_COMMON_DIALOGS_H
#include "os2/include/os2win.h"

enum
{
    CDERR_INITIALIZATION = 2,
    CDERR_NOTEMPLATE = 3,
    CDERR_FINDRESFAILURE = 6,
    CDERR_MEMALLOCFAILURE = 9,
    CDERR_NOHOOK = 11,
    CF_ENABLEHOOK = 8,
    CF_ENABLETEMPLATE = 0x10,
    CF_ENABLETEMPLATEHANDLE = 0x20,
    CF_INITTOLOGFONTSTRUCT = 0x40,
    CC_RGBINIT = 1,
    CC_PREVENTFULLOPEN = 4,
    CC_SHOWHELP = 8,
    CC_ENABLEHOOK = 0x10,
    CC_ENABLETEMPLATE = 0x20,
    CC_ENABLETEMPLATEHANDLE = 0x40,
    WIN_INITDIALOG = 0x110,
    WIN_COMMAND = 0x111,
    WIN_DESTROY = 2,
    EN_CHANGE_CODE = 4
};

/* Translates one PM dialog message for a Windows hook; nonzero means the hook consumed it. */
ULONG open32CommonDialogHook(ULONG hook, ULONG window, ULONG message, ULONG first, ULONG second);
/* The template name is a resource number or a guest string. */
ULONG open32DialogTemplateIdentity(ULONG module, ULONG name);
/* The module whose resources hold a template; 0 and -1 name the program. */
ULONG open32DialogTemplateModule(ULONG module);
/* The guest address of a host view of a caller's record; 0 when it is not guest memory. */
ULONG open32GuestRecordAddress(PVOID record, ULONG size);
#endif
