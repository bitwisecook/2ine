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

    enum
    {
        FDS_CENTER = 0x00000001,
        FDS_CUSTOM = 0x00000002,
        FDS_FILTERUNION = 0x00000004,
        FDS_HELPBUTTON = 0x00000008,
        FDS_APPLYBUTTON = 0x00000010,
        FDS_PRELOAD_VOLINFO = 0x00000020,
        FDS_MODELESS = 0x00000040,
        FDS_INCLUDE_EAS = 0x00000080,
        FDS_OPEN_DIALOG = 0x00000100,
        FDS_SAVEAS_DIALOG = 0x00000200,
        FDS_MULTIPLESEL = 0x00000400,
        FDS_ENABLEFILELB = 0x00000800
    };

    enum
    {
        FDS_EFSELECTION = 0,
        FDS_LBSELECTION = 1
    };

    enum
    {
        FDS_SUCCESSFUL = 0,
        FDS_ERR_DEALLOCATE_MEMORY = 1,
        FDS_ERR_FILTER_TRUNC = 2,
        FDS_ERR_INVALID_DIALOG = 3,
        FDS_ERR_INVALID_DRIVE = 4,
        FDS_ERR_INVALID_FILTER = 5,
        FDS_ERR_INVALID_PATHFILE = 6,
        FDS_ERR_OUT_OF_MEMORY = 7,
        FDS_ERR_PATH_TOO_LONG = 8,
        FDS_ERR_TOO_MANY_FILE_TYPES = 9,
        FDS_ERR_INVALID_VERSION = 10,
        FDS_ERR_INVALID_CUSTOM_HANDLE = 11,
        FDS_ERR_DIALOG_LOAD_ERROR = 12,
        FDS_ERR_DRIVE_ERROR = 13
    };

    enum
    {
        FDM_FILTER = WM_USER + 40,
        FDM_VALIDATE = WM_USER + 41,
        FDM_ERROR = WM_USER + 42
    };

    enum
    {
        DID_FILE_DIALOG = 256,
        DID_FILENAME_TXT = 257,
        DID_FILENAME_ED = 258,
        DID_DRIVE_TXT = 259,
        DID_DRIVE_CB = 260,
        DID_FILTER_TXT = 261,
        DID_FILTER_CB = 262,
        DID_DIRECTORY_TXT = 263,
        DID_DIRECTORY_LB = 264,
        DID_FILES_TXT = 265,
        DID_FILES_LB = 266,
        DID_HELP_PB = 267,
        DID_APPLY_PB = 268,
        DID_READ_ONLY = 269,
        DID_DIRECTORY_SELECTED = 270,
        DID_OK_PB = DID_OK,
        DID_CANCEL_PB = DID_CANCEL
    };

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

    enum
    {
        FNTS_CENTER = 0x00000001,
        FNTS_CUSTOM = 0x00000002,
        FNTS_OWNERDRAWPREVIEW = 0x00000004,
        FNTS_HELPBUTTON = 0x00000008,
        FNTS_APPLYBUTTON = 0x00000010,
        FNTS_RESETBUTTON = 0x00000020,
        FNTS_MODELESS = 0x00000040,
        FNTS_INITFROMFATTRS = 0x00000080,
        FNTS_BITMAPONLY = 0x00000100,
        FNTS_VECTORONLY = 0x00000200,
        FNTS_FIXEDWIDTHONLY = 0x00000400,
        FNTS_PROPORTIONALONLY = 0x00000800,
        FNTS_NOSYNTHESIZEDFONTS = 0x00001000
    };

    enum
    {
        CLRC_FOREGROUND = 1,
        CLRC_BACKGROUND = 2
    };

    enum
    {
        FNTI_BITMAPFONT = 0x0001,
        FNTI_VECTORFONT = 0x0002,
        FNTI_FIXEDWIDTHFONT = 0x0004,
        FNTI_PROPORTIONALFONT = 0x0008,
        FNTI_SYNTHESIZED = 0x0010,
        FNTI_DEFAULTLIST = 0x0020,
        FNTI_FAMILYNAME = 0x0100,
        FNTI_STYLENAME = 0x0200,
        FNTI_POINTSIZE = 0x0400
    };

    enum
    {
        FNTS_SUCCESSFUL = 0,
        FNTS_ERR_INVALID_DIALOG = 3,
        FNTS_ERR_ALLOC_SHARED_MEM = 4,
        FNTS_ERR_INVALID_PARM = 5,
        FNTS_ERR_OUT_OF_MEMORY = 7,
        FNTS_ERR_INVALID_VERSION = 10,
        FNTS_ERR_DIALOG_LOAD_ERROR = 12
    };

    enum
    {
        FNTM_FACENAMECHANGED = WM_USER + 50,
        FNTM_POINTSIZECHANGED = WM_USER + 51,
        FNTM_STYLECHANGED = WM_USER + 52,
        FNTM_COLORCHANGED = WM_USER + 53,
        FNTM_UPDATEPREVIEW = WM_USER + 54,
        FNTM_FILTERLIST = WM_USER + 55
    };

    enum
    {
        DID_FONT_DIALOG = 300,
        DID_NAME = 301,
        DID_STYLE = 302,
        DID_DISPLAY_FILTER = 303,
        DID_PRINTER_FILTER = 304,
        DID_SIZE = 305,
        DID_SAMPLE = 306,
        DID_OUTLINE = 307,
        DID_UNDERSCORE = 308,
        DID_STRIKEOUT = 309,
        DID_HELP_BUTTON = 310,
        DID_APPLY_BUTTON = 311,
        DID_RESET_BUTTON = 312,
        DID_OK_BUTTON = DID_OK,
        DID_CANCEL_BUTTON = DID_CANCEL,
        DID_NAME_PREFIX = 313,
        DID_STYLE_PREFIX = 314,
        DID_SIZE_PREFIX = 315,
        DID_SAMPLE_GROUPBOX = 316,
        DID_EMPHASIS_GROUPBOX = 317,
        DID_FONT_ISO_SUPPORT = 318,
        DID_FONT_ISO_UNTESTED = 319
    };

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

    /* INCL_WINSTDSPIN */

    enum
    {
        SPBS_ALLCHARACTERS = 0x00000000,
        SPBS_NUMERICONLY = 0x00000001,
        SPBS_READONLY = 0x00000002,
        SPBS_MASTER = 0x00000010,
        SPBS_SERVANT = 0x00000000,
        SPBS_JUSTDEFAULT = 0x00000000,
        SPBS_JUSTLEFT = 0x00000008,
        SPBS_JUSTRIGHT = 0x00000004,
        SPBS_JUSTCENTER = 0x0000000C,
        SPBS_NOBORDER = 0x00000020,
        SPBS_FASTSPIN = 0x00000100,
        SPBS_PADWITHZEROS = 0x00000080
    };

    enum
    {
        SPBN_UPARROW = 0x20A,
        SPBN_DOWNARROW = 0x20B,
        SPBN_ENDSPIN = 0x20C,
        SPBN_CHANGE = 0x20D,
        SPBN_SETFOCUS = 0x20E,
        SPBN_KILLFOCUS = 0x20F
    };

    enum
    {
        SPBM_OVERRIDESETLIMITS = 0x200,
        SPBM_QUERYLIMITS = 0x201,
        SPBM_SETTEXTLIMIT = 0x202,
        SPBM_SPINUP = 0x203,
        SPBM_SPINDOWN = 0x204,
        SPBM_QUERYVALUE = 0x205,
        SPBM_SETARRAY = 0x206,
        SPBM_SETLIMITS = 0x207,
        SPBM_SETCURRENTVALUE = 0x208,
        SPBM_SETMASTER = 0x209
    };

    enum
    {
        SPBQ_UPDATEIFVALID = 0,
        SPBQ_ALWAYSUPDATE = 1,
        SPBQ_DONOTUPDATE = 3
    };

    /* INCL_WINSTDDRAG */

    enum
    {
        DM_DROP = 0x032f,
        DM_DRAGOVER = 0x032e,
        DM_DRAGLEAVE = 0x032d,
        DM_DROPHELP = 0x032c,
        DM_ENDCONVERSATION = 0x032b,
        DM_PRINT = 0x032a,
        DM_RENDER = 0x0329,
        DM_RENDERCOMPLETE = 0x0328,
        DM_RENDERPREPARE = 0x0327,
        DM_DRAGFILECOMPLETE = 0x0326,
        DM_EMPHASIZETARGET = 0x0325,
        DM_DRAGERROR = 0x0324,
        DM_FILERENDERED = 0x0323,
        DM_RENDERFILE = 0x0322,
        DM_DRAGOVERNOTIFY = 0x0321,
        DM_PRINTOBJECT = 0x0320,
        DM_DISCARDOBJECT = 0x031f,
        DM_DROPNOTIFY = 0x031e
    };

    enum
    {
        DOR_NODROP = 0x0000,
        DOR_DROP = 0x0001,
        DOR_NODROPOP = 0x0002,
        DOR_NEVERDROP = 0x0003
    };

    enum
    {
        DO_COPYABLE = 0x0001,
        DO_MOVEABLE = 0x0002,
        DO_LINKABLE = 0x0004,
        DO_CREATEABLE = 0x0008,
        DO_CREATEPROGRAMOBJECTABLE = 0x0010
    };

    enum
    {
        DC_OPEN = 0x0001,
        DC_REF = 0x0002,
        DC_GROUP = 0x0004,
        DC_CONTAINER = 0x0008,
        DC_PREPARE = 0x0010,
        DC_REMOVEABLEMEDIA = 0x0020
    };

    enum
    {
        DO_DEFAULT = 0xBFFE,
        DO_UNKNOWN = 0xBFFF,
        DO_COPY = 0x0010,
        DO_MOVE = 0x0020,
        DO_LINK = 0x0018,
        DO_CREATE = 0x0040,
        DO_CREATEPROGRAMOBJECT = 0x0080
    };

    enum
    {
        DMFL_TARGETSUCCESSFUL = 0x0001,
        DMFL_TARGETFAIL = 0x0002,
        DMFL_NATIVERENDER = 0x0004,
        DMFL_RENDERRETRY = 0x0008,
        DMFL_RENDEROK = 0x0010,
        DMFL_RENDERFAIL = 0x0020
    };

    enum
    {
        DRG_ICON = 0x00000001,
        DRG_BITMAP = 0x00000002,
        DRG_POLYGON = 0x00000004,
        DRG_STRETCH = 0x00000008,
        DRG_TRANSPARENT = 0x00000010,
        DRG_CLOSED = 0x00000020,
        DRG_MINIBITMAP = 0x00000040
    };

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
