/* Public domain. No rights reserved. Wholly AI-authored. */
/* Standard dialog records retain 32-bit guest addresses on every host. */
#ifndef _INCL_PMSTDDLG_H_
#define _INCL_PMSTDDLG_H_

#include "os2/include/pmwin.h"
#include "os2/include/pmfont.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* INCL_WINSTDFILE */

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

    OS2EXPORT HWND OS2API WinFileDlg(HWND parent, HWND owner, PVOID data);

    OS2EXPORT MRESULT OS2API WinDefFileDlgProc(HWND window, ULONG message, MPARAM first,
                                               MPARAM second);

    OS2EXPORT BOOL OS2API WinFreeFileDlgList(PVOID list);

    /* INCL_WINSTDFONT */

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

    OS2EXPORT MRESULT OS2API WinDefFontDlgProc(HWND window, ULONG message, MPARAM first,
                                               MPARAM second);

    /* INCL_WINSTDDRAG */

    OS2EXPORT ULONG OS2API DrgAddStrHandle(PSZ name);

    OS2EXPORT BOOL OS2API DrgDeleteStrHandle(ULONG string);

    OS2EXPORT ULONG OS2API DrgQueryStrName(ULONG string, ULONG capacity, PSZ output);

    OS2EXPORT ULONG OS2API DrgQueryStrNameLen(ULONG string);

    OS2EXPORT BOOL OS2API DrgAccessDraginfo(ULONG address);

    OS2EXPORT ULONG OS2API DrgAllocDraginfo(ULONG count);

    OS2EXPORT ULONG OS2API DrgAllocDragtransfer(ULONG count);

    OS2EXPORT BOOL OS2API DrgDeleteDraginfoStrHandles(ULONG address);

    OS2EXPORT HWND OS2API DrgDrag(HWND source, ULONG info, ULONG images, ULONG count,
                                  LONG terminate, ULONG reserved);

    OS2EXPORT BOOL OS2API DrgFreeDraginfo(ULONG address);

    OS2EXPORT BOOL OS2API DrgFreeDragtransfer(ULONG address);

    OS2EXPORT HPS OS2API DrgGetPS(HWND window);

    OS2EXPORT BOOL OS2API DrgPostTransferMsg(HWND window, ULONG message, ULONG transfer,
                                             ULONG flags, ULONG reserved, BOOL retry);

    OS2EXPORT ULONG OS2API DrgQueryDragitemCount(ULONG address);

    OS2EXPORT ULONG OS2API DrgQueryDragitemPtr(ULONG address, ULONG index);

    OS2EXPORT BOOL OS2API DrgReleasePS(HPS space);

    OS2EXPORT MRESULT OS2API DrgSendTransferMsg(HWND window, ULONG message, MPARAM first,
                                                MPARAM second);

    OS2EXPORT BOOL OS2API DrgSetDragitem(ULONG address, ULONG item, ULONG bytes, ULONG index);

    OS2EXPORT BOOL OS2API DrgVerifyRMF(ULONG item_address, PSZ mechanism, PSZ format);

#ifdef __cplusplus
}
#endif

#endif
