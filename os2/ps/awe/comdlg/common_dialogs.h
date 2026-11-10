/* Public domain. No rights reserved. Wholly AI-authored. */
/* Hook and template helpers shared by the colour and font common dialogs. */
#ifndef TWOINE_PMWINX_COMMON_DIALOGS_H
#define TWOINE_PMWINX_COMMON_DIALOGS_H
#include "os2/include/os2win.h"

/* Translates one PM dialog message for a Windows hook; nonzero means the hook consumed it. */
ULONG open32CommonDialogHook(ULONG hook, ULONG window, ULONG message, ULONG first, ULONG second);
/* The template name is a resource number or a guest string. */
ULONG open32DialogTemplateIdentity(ULONG module, ULONG name);
/* The module whose resources hold a template; 0 and -1 name the program. */
ULONG open32DialogTemplateModule(ULONG module);
/* The guest address of a host view of a caller's record; 0 when it is not guest memory. */
ULONG open32GuestRecordAddress(PVOID record, ULONG size);
#endif
