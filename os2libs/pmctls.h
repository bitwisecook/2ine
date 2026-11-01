/* Public domain. No rights reserved. Wholly AI-authored. */
/* Standard dialog records retain 32-bit guest addresses on every host. */
#ifndef TWOINE_PMCTLS_H
#define TWOINE_PMCTLS_H
#include "os2libs/pmwin.h"
#pragma pack(push, 4)
typedef struct
{
    ULONG cbSize, fl, ulUser;
    LONG lReturn, lSRC;
    ULONG pszTitle, pszOKButton, pfnDlgProc, pszIType, papszITypeList;
    ULONG pszIDrive, papszIDriveList, hMod;
    CHAR szFullFile[260];
    ULONG papszFQFilename, ulFQFCount;
    USHORT usDlgId;
    SHORT x, y, sEAType;
} LXFILEDLG;
#pragma pack(pop)
OS2EXPORT HWND OS2API WinFileDlg(HWND parent, HWND owner, PVOID data) OS2APIINFO(4);
OS2EXPORT MRESULT OS2API WinDefFileDlgProc(HWND window, ULONG message, MPARAM first, MPARAM second)
    OS2APIINFO(5);
OS2EXPORT BOOL OS2API WinFreeFileDlgList(PVOID list) OS2APIINFO(6);
#endif
