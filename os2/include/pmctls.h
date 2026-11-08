/* Public domain. No rights reserved. Wholly AI-authored. */
/* Standard dialog records retain 32-bit guest addresses on every host. */
#ifndef TWOINE_PMCTLS_H
#define TWOINE_PMCTLS_H
#include "os2/include/pmwin.h"
#include "os2/include/private/pmgpip.h"
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
#pragma pack(push, 2)

typedef struct LXFONTDLG
{
    ULONG cbSize, hpsScreen, hpsPrinter, pszTitle, pszPreview, pszPtSizeList, pfnDlgProc,
        pszFamilyname;
    LONG fxPointSize;
    ULONG fl, flFlags, flType, flTypeMask, flStyle, flStyleMask;
    LONG clrFore, clrBack;
    ULONG ulUser;
    LONG lReturn, lSRC, lEmHeight, lXHeight, lExternalLeading;
    ULONG hMod;
    FATTRS fAttrs;
    SHORT sNominalPointSize;
    USHORT usWeight, usWidth;
    SHORT x, y;
    USHORT usDlgId, usFamilyBufLen, usReserved;
} LXFONTDLG;

#pragma pack(pop)
OS2EXPORT HWND OS2API WinFontDlg(HWND parent, HWND owner, PVOID data);
OS2EXPORT MRESULT OS2API WinDefFontDlgProc(HWND window, ULONG message, MPARAM first, MPARAM second);
OS2EXPORT HWND OS2API WinFileDlg(HWND parent, HWND owner, PVOID data);
OS2EXPORT MRESULT OS2API WinDefFileDlgProc(HWND window, ULONG message, MPARAM first, MPARAM second);
OS2EXPORT BOOL OS2API WinFreeFileDlgList(PVOID list);
#endif
