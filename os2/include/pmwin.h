/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#ifndef _INCL_PMWIN_H_
#define _INCL_PMWIN_H_

#include "os2/include/os2def.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Common window manager declarations (the Toolkit's non-INCL_NOCOMMON set) */

    typedef MRESULT(OS2API FNWP)(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);

    typedef FNWP *PFNWP;

    enum
    {
        CS_MOVENOTIFY = 0x00000001,
        CS_SIZEREDRAW = 0x00000004,
        CS_HITTEST = 0x00000008,
        CS_PUBLIC = 0x00000010,
        CS_FRAME = 0x00000020,
        CS_SYNCPAINT = 0x02000000,
        CS_PARENTCLIP = 0x08000000,
        CS_SAVEBITS = 0x04000000,
        CS_CLIPSIBLINGS = 0x10000000,
        CS_CLIPCHILDREN = 0x20000000
    };

    enum
    {
        WS_VISIBLE = 0x80000000,
        WS_DISABLED = 0x40000000,
        WS_CLIPCHILDREN = 0x20000000,
        WS_CLIPSIBLINGS = 0x10000000,
        WS_PARENTCLIP = 0x08000000,
        WS_SAVEBITS = 0x04000000,
        WS_SYNCPAINT = 0x02000000,
        WS_MINIMIZED = 0x01000000,
        WS_MAXIMIZED = 0x00800000,
        WS_ANIMATE = 0x00400000,
        WS_GROUP = 0x00010000,
        WS_TABSTOP = 0x00020000,
        WS_MULTISELECT = 0x00040000
    };

    /* WinDrawBitmap flags */
    enum
    {
        DBM_NORMAL = 0x0000,
        DBM_INVERT = 0x0001,
        DBM_HALFTONE = 0x0002,
        DBM_STRETCH = 0x0004,
        DBM_IMAGEATTRS = 0x0008,
        DBM_XOR = 0x0010
    };

    /* WinDrawText flags */
    enum
    {
        DT_LEFT = 0x00000000,
        DT_QUERYEXTENT = 0x00000002,
        DT_UNDERSCORE = 0x00000010,
        DT_STRIKEOUT = 0x00000020,
        DT_TEXTATTRS = 0x00000040,
        DT_EXTERNALLEADING = 0x00000080,
        DT_CENTER = 0x00000100,
        DT_RIGHT = 0x00000200,
        DT_TOP = 0x00000000,
        DT_VCENTER = 0x00000400,
        DT_BOTTOM = 0x00000800,
        DT_HALFTONE = 0x00001000,
        DT_MNEMONIC = 0x00002000,
        DT_WORDBREAK = 0x00004000,
        DT_ERASERECT = 0x00008000
    };

// magic HWND identifiers
#define HWND_DESKTOP 1

#define HWND_OBJECT 2

#define HWND_TOP 3

#define HWND_BOTTOM 4

    /* WinQueryWindow codes */
    enum
    {
        QW_NEXT = 0,
        QW_PREV = 1,
        QW_TOP = 2,
        QW_BOTTOM = 3,
        QW_OWNER = 4,
        QW_PARENT = 5,
        QW_NEXTTOP = 6,
        QW_PREVTOP = 7,
        QW_FRAMEOWNER = 8
    };

    // built-in window classes (not real strings, like user classes would be here).
    enum
    {
        WC_FRAME = 0xFFFF0001,
        WC_COMBOBOX = 0xFFFF0002,
        WC_BUTTON = 0xFFFF0003,
        WC_MENU = 0xFFFF0004,
        WC_STATIC = 0xFFFF0005,
        WC_ENTRYFIELD = 0xFFFF0006,
        WC_LISTBOX = 0xFFFF0007,
        WC_SCROLLBAR = 0xFFFF0008,
        WC_TITLEBAR = 0xFFFF0009,
        WC_MLE = 0xFFFF000A,
        WC_APPSTAT = 0xFFFF0010,
        WC_KBDSTAT = 0xFFFF0011,
        WC_PECIC = 0xFFFF0012,
        WC_DBE_KKPOPUP = 0xFFFF0013,
        WC_SPINBUTTON = 0xFFFF0020,
        WC_CONTAINER = 0xFFFF0025,
        WC_SLIDER = 0xFFFF0026,
        WC_VALUESET = 0xFFFF0027,
        WC_NOTEBOOK = 0xFFFF0028,
        WC_CIRCULARSLIDER = 0xFFFF0041
    };

    // used by WM_ADJUSTWINDOWPOS, etc.
    enum
    {
        SWP_SIZE = 0x0001,
        SWP_MOVE = 0x0002,
        SWP_ZORDER = 0x0004,
        SWP_SHOW = 0x0008,
        SWP_HIDE = 0x0010,
        SWP_NOREDRAW = 0x0020,
        SWP_NOADJUST = 0x0040,
        SWP_ACTIVATE = 0x0080,
        SWP_DEACTIVATE = 0x0100,
        SWP_EXTSTATECHANGE = 0x0200,
        SWP_MINIMIZE = 0x0400,
        SWP_MAXIMIZE = 0x0800,
        SWP_RESTORE = 0x1000,
        SWP_FOCUSACTIVATE = 0x2000,
        SWP_FOCUSDEACTIVATE = 0x4000,
        SWP_NOAUTOCLOSE = 0x8000
    };

    /* Static controls (INCL_WINSTATICS) */
    enum
    {
        SS_TEXT = 0x0001,
        SS_GROUPBOX = 0x0002,
        SS_ICON = 0x0003,
        SS_BITMAP = 0x0004,
        SS_FGNDRECT = 0x0005,
        SS_HALFTONERECT = 0x0006,
        SS_BKGNDRECT = 0x0007,
        SS_FGNDFRAME = 0x0008,
        SS_HALFTONEFRAME = 0x0009,
        SS_BKGNDFRAME = 0x000a,
        SS_SYSICON = 0x000b,
        SS_AUTOSIZE = 0x0040,
        SM_SETHANDLE = 0x0100,
        SM_QUERYHANDLE = 0x0101
    };

    /* INCL_WINBUTTONS */
    enum
    {
        BS_PUSHBUTTON = 0,
        BS_CHECKBOX = 1,
        BS_AUTOCHECKBOX = 2,
        BS_RADIOBUTTON = 3,
        BS_AUTORADIOBUTTON = 4,
        BS_3STATE = 5,
        BS_AUTO3STATE = 6,
        BS_USERBUTTON = 7,
        BS_NOTEBOOKBUTTON = 8,
        BS_PRIMARYSTYLES = 0x000f,
        BS_TEXT = 0x0010,
        BS_MINIICON = 0x0020,
        BS_BITMAP = 0x0040,
        BS_ICON = 0x0080,
        BS_HELP = 0x0100,
        BS_SYSCOMMAND = 0x0200,
        BS_DEFAULT = 0x0400,
        BS_NOPOINTERFOCUS = 0x0800,
        BS_NOBORDER = 0x1000,
        BS_NOCURSORSELECT = 0x2000,
        BS_AUTOSIZE = 0x4000
    };

    enum
    {
        BM_CLICK = 0x0120,
        BM_QUERYCHECKINDEX = 0x0121,
        BM_QUERYHILITE = 0x0122,
        BM_SETHILITE = 0x0123,
        BM_QUERYCHECK = 0x0124,
        BM_SETCHECK = 0x0125,
        BM_SETDEFAULT = 0x0126,
        BM_AUTOSIZE = 0x0128
    };

    enum
    {
        BN_CLICKED = 1,
        BN_DBLCLICKED = 2,
        BN_PAINT = 3
    };

    enum
    {
        BDS_HILITED = 0x0100,
        BDS_DISABLED = 0x0200,
        BDS_DEFAULT = 0x0400
    };

    /* INCL_WINENTRYFIELDS */
    enum
    {
        ES_LEFT = 0x00000000,
        ES_CENTER = 0x00000001,
        ES_RIGHT = 0x00000002,
        ES_AUTOSCROLL = 0x00000004,
        ES_MARGIN = 0x00000008,
        ES_AUTOTAB = 0x00000010,
        ES_READONLY = 0x00000020,
        ES_COMMAND = 0x00000040,
        ES_UNREADABLE = 0x00000080,
        ES_AUTOSIZE = 0x00000200
    };

    enum
    {
        CBS_SIMPLE = 0x0001,
        CBS_DROPDOWN = 0x0002,
        CBS_DROPDOWNLIST = 0x0004,
        CBS_COMPATIBLE = 0x0008
    };

    enum
    {
        CBID_LIST = 0x029A,
        CBID_EDIT = 0x029B
    };

    enum
    {
        CBM_SHOWLIST = 0x0170,
        CBM_HILITE = 0x0171,
        CBM_ISLISTSHOWING = 0x0172
    };

    enum
    {
        CBN_EFCHANGE = 1,
        CBN_EFSCROLL = 2,
        CBN_MEMERROR = 3,
        CBN_LBSELECT = 4,
        CBN_LBSCROLL = 5,
        CBN_SHOWLIST = 6,
        CBN_ENTER = 7
    };

    enum
    {
        EM_QUERYCHANGED = 0x0140,
        EM_QUERYSEL = 0x0141,
        EM_SETSEL = 0x0142,
        EM_SETTEXTLIMIT = 0x0143,
        EM_CUT = 0x0144,
        EM_COPY = 0x0145,
        EM_CLEAR = 0x0146,
        EM_PASTE = 0x0147,
        EM_QUERYFIRSTCHAR = 0x0148,
        EM_SETFIRSTCHAR = 0x0149,
        EM_QUERYREADONLY = 0x014a,
        EM_SETREADONLY = 0x014b,
        EM_SETINSERTMODE = 0x014c
    };

    enum
    {
        EN_SETFOCUS = 0x0001,
        EN_KILLFOCUS = 0x0002,
        EN_CHANGE = 0x0004,
        EN_SCROLL = 0x0008,
        EN_MEMERROR = 0x0010,
        EN_OVERFLOW = 0x0020,
        EN_INSERTMODETOGGLE = 0x0040
    };

    /* INCL_WINMLE (the Toolkit's pmmle.h) */
    enum
    {
        MLS_WORDWRAP = 0x00000001,
        MLS_BORDER = 0x00000002,
        MLS_VSCROLL = 0x00000004,
        MLS_HSCROLL = 0x00000008,
        MLS_READONLY = 0x00000010,
        MLS_IGNORETAB = 0x00000020,
        MLS_DISABLEUNDO = 0x00000040,
        MLS_LIMITVSCROLL = 0x00000080
    };

    enum
    {
        MLFIE_CFTEXT = 0,
        MLFIE_NOTRANS = 1,
        MLFIE_WINFMT = 2,
        MLFIE_RTF = 3
    };

    enum
    {
        MLFQS_MINMAXSEL = 0,
        MLFQS_MINSEL = 1,
        MLFQS_MAXSEL = 2,
        MLFQS_ANCHORSEL = 3,
        MLFQS_CURSORSEL = 4
    };

    enum
    {
        MLM_SETTEXTLIMIT = 0x01b0,
        MLM_QUERYTEXTLIMIT = 0x01b1,
        MLM_SETFORMATRECT = 0x01b2,
        MLM_QUERYFORMATRECT = 0x01b3,
        MLM_SETWRAP = 0x01b4,
        MLM_QUERYWRAP = 0x01b5,
        MLM_SETTABSTOP = 0x01b6,
        MLM_QUERYTABSTOP = 0x01b7,
        MLM_SETREADONLY = 0x01b8,
        MLM_QUERYREADONLY = 0x01b9,
        MLM_QUERYCHANGED = 0x01ba,
        MLM_SETCHANGED = 0x01bb,
        MLM_QUERYLINECOUNT = 0x01bc,
        MLM_CHARFROMLINE = 0x01bd,
        MLM_LINEFROMCHAR = 0x01be,
        MLM_QUERYLINELENGTH = 0x01bf,
        MLM_QUERYTEXTLENGTH = 0x01c0,
        MLM_FORMAT = 0x01c1,
        MLM_SETIMPORTEXPORT = 0x01c2,
        MLM_IMPORT = 0x01c3,
        MLM_EXPORT = 0x01c4,
        MLM_DELETE = 0x01c6,
        MLM_QUERYFORMATLINELENGTH = 0x01c7,
        MLM_QUERYFORMATTEXTLENGTH = 0x01c8,
        MLM_INSERT = 0x01c9,
        MLM_SETSEL = 0x01ca,
        MLM_QUERYSEL = 0x01cb,
        MLM_QUERYSELTEXT = 0x01cc,
        MLM_QUERYUNDO = 0x01cd,
        MLM_UNDO = 0x01ce,
        MLM_RESETUNDO = 0x01cf,
        MLM_QUERYFONT = 0x01d0,
        MLM_SETFONT = 0x01d1,
        MLM_SETTEXTCOLOR = 0x01d2,
        MLM_QUERYTEXTCOLOR = 0x01d3,
        MLM_SETBACKCOLOR = 0x01d4,
        MLM_QUERYBACKCOLOR = 0x01d5,
        MLM_QUERYFIRSTCHAR = 0x01d6,
        MLM_SETFIRSTCHAR = 0x01d7,
        MLM_CUT = 0x01d8,
        MLM_COPY = 0x01d9,
        MLM_PASTE = 0x01da,
        MLM_CLEAR = 0x01db,
        MLM_ENABLEREFRESH = 0x01dc,
        MLM_DISABLEREFRESH = 0x01dd,
        MLM_SEARCH = 0x01de,
        MLM_QUERYIMPORTEXPORT = 0x01df
    };

    enum
    {
        MLN_OVERFLOW = 0x0001,
        MLN_PIXHORZOVERFLOW = 0x0002,
        MLN_PIXVERTOVERFLOW = 0x0003,
        MLN_TEXTOVERFLOW = 0x0004,
        MLN_VSCROLL = 0x0005,
        MLN_HSCROLL = 0x0006,
        MLN_CHANGE = 0x0007,
        MLN_SETFOCUS = 0x0008,
        MLN_KILLFOCUS = 0x0009,
        MLN_MARGIN = 0x000a,
        MLN_SEARCHPAUSE = 0x000b,
        MLN_MEMERROR = 0x000c,
        MLN_UNDOOVERFLOW = 0x000d,
        MLN_CLPBDFAIL = 0x000f
    };

    /* INCL_WINLISTBOXES */
    enum
    {
        LS_MULTIPLESEL = 0x00000001,
        LS_OWNERDRAW = 0x00000002,
        LS_NOADJUSTPOS = 0x00000004,
        LS_HORZSCROLL = 0x00000008,
        LS_EXTENDEDSEL = 0x00000010
    };

    enum
    {
        LN_SELECT = 1,
        LN_SETFOCUS = 2,
        LN_KILLFOCUS = 3,
        LN_SCROLL = 4,
        LN_ENTER = 5
    };

    enum
    {
        LM_QUERYITEMCOUNT = 0x0160,
        LM_INSERTITEM = 0x0161,
        LM_SETTOPINDEX = 0x0162,
        LM_DELETEITEM = 0x0163,
        LM_SELECTITEM = 0x0164,
        LM_QUERYSELECTION = 0x0165,
        LM_SETITEMTEXT = 0x0166,
        LM_QUERYITEMTEXTLENGTH = 0x0167,
        LM_QUERYITEMTEXT = 0x0168,
        LM_SETITEMHANDLE = 0x0169,
        LM_QUERYITEMHANDLE = 0x016a,
        LM_SEARCHSTRING = 0x016b,
        LM_SETITEMHEIGHT = 0x016c,
        LM_QUERYTOPINDEX = 0x016d,
        LM_DELETEALL = 0x016e,
        LM_INSERTMULTITEMS = 0x016f,
        LM_SETITEMWIDTH = 0x0660
    };

    enum
    {
        LIT_CURSOR = -4,
        LIT_ERROR = -3,
        LIT_MEMERROR = -2,
        LIT_NONE = -1,
        LIT_FIRST = -1,
        LIT_END = -1,
        LIT_SORTASCENDING = -2,
        LIT_SORTDESCENDING = -3
    };

    enum
    {
        LSS_SUBSTRING = 0x0001,
        LSS_PREFIX = 0x0002,
        LSS_CASESENSITIVE = 0x0004
    };

    /* INCL_WINSCROLLBARS */
    enum
    {
        SBS_HORZ = 0,
        SBS_VERT = 1,
        SBS_THUMBSIZE = 2,
        SBS_AUTOTRACK = 4,
        SBS_AUTOSIZE = 0x2000
    };

    enum
    {
        SBM_SETSCROLLBAR = 0x01a0,
        SBM_SETPOS = 0x01a1,
        SBM_QUERYPOS = 0x01a2,
        SBM_QUERYRANGE = 0x01a3,
        SBM_SETTHUMBSIZE = 0x01a6
    };

    enum
    {
        SB_LINEUP = 1,
        SB_LINEDOWN = 2,
        SB_LINELEFT = 1,
        SB_LINERIGHT = 2,
        SB_PAGEUP = 3,
        SB_PAGEDOWN = 4,
        SB_PAGELEFT = 3,
        SB_PAGERIGHT = 4,
        SB_SLIDERTRACK = 5,
        SB_SLIDERPOSITION = 6,
        SB_ENDSCROLL = 7
    };

    /* WinScrollWindow() flags */
    enum
    {
        SW_SCROLLCHILDREN = 0x0001,
        SW_INVALIDATERGN = 0x0002
    };

    /* WinDrawBorder() flags */
    enum
    {
        DB_PATCOPY = 0x0000,
        DB_PATINVERT = 0x0001,
        DB_DESTINVERT = 0x0002,
        DB_AREAMIXMODE = 0x0003,
        DB_ROP = 0x0007,
        DB_INTERIOR = 0x0008,
        DB_AREAATTRS = 0x0010,
        DB_STANDARD = 0x0100,
        DB_DLGBORDER = 0x0200
    };

    /* WinGetClipPS() flags */
    enum
    {
        PSF_LOCKWINDOWUPDATE = 0x0001,
        PSF_CLIPUPWARDS = 0x0002,
        PSF_CLIPDOWNWARDS = 0x0004,
        PSF_CLIPSIBLINGS = 0x0008,
        PSF_CLIPCHILDREN = 0x0010,
        PSF_PARENTCLIP = 0x0020
    };

    typedef struct
    {
        ULONG fl;
        LONG cy;
        LONG cx;
        LONG y;
        LONG x;
        HWND hwndInsertBehind;
        HWND hwnd;
        ULONG ulReserved1;
        ULONG ulReserved2;
    } SWP, *PSWP;

    OS2EXPORT BOOL OS2API WinIsChild(HWND child, HWND parent);

    OS2EXPORT BOOL OS2API WinSetWindowText(HWND window, PSZ text);

    OS2EXPORT LONG OS2API WinQueryWindowText(HWND window, LONG capacity, PCHAR output);

    OS2EXPORT LONG OS2API WinQueryWindowTextLength(HWND window);

    OS2EXPORT ULONG OS2API WinQueryVisibleRegion(HWND window, ULONG region);

    OS2EXPORT BOOL OS2API WinSetVisibleRegionNotify(HWND window, BOOL enable);

    OS2EXPORT BOOL OS2API WinQueryWindowPos(HWND hwnd, PSWP position);

    OS2EXPORT BOOL OS2API WinDrawBitmap(HPS destination, ULONG bitmap, PRECTL source, PPOINTL point,
                                        LONG foreground, LONG background, ULONG flags);

    OS2EXPORT BOOL OS2API WinDrawBorder(HPS ps, PRECTL rect, LONG cx, LONG cy, LONG foreground,
                                        LONG background, ULONG flags);

    OS2EXPORT BOOL OS2API WinEnableWindowUpdate(HWND window, BOOL enable);

    OS2EXPORT BOOL OS2API WinInvalidateRect(HWND window, PRECTL rectangle, BOOL children);

    OS2EXPORT BOOL OS2API WinUpdateWindow(HWND window);

    OS2EXPORT LONG OS2API WinDrawText(HPS ps, LONG count, PCH text, PRECTL rect, LONG foreground,
                                      LONG background, ULONG flags);

    OS2EXPORT BOOL OS2API WinEnableWindow(HWND window, BOOL enable);

    OS2EXPORT BOOL OS2API WinSetOwner(HWND window, HWND owner);

    OS2EXPORT HWND OS2API WinQueryWindow(HWND window, LONG code);

    OS2EXPORT BOOL OS2API WinSetParent(HWND window, HWND parent, BOOL redraw);

    OS2EXPORT LONG OS2API WinLoadString(HAB hab, HMODULE module, ULONG id, LONG capacity,
                                        PCHAR output);

    OS2EXPORT HWND OS2API WinWindowFromID(HWND window, ULONG id);

    OS2EXPORT BOOL OS2API WinInvertRect(HPS ps, PRECTL rect);

    OS2EXPORT BOOL OS2API WinSetWindowPos(HWND hwnd, HWND behind, LONG x, LONG y, LONG cx, LONG cy,
                                          ULONG flags);

    OS2EXPORT HWND OS2API WinCreateWindow(HWND hwndParent, PSZ pszClass, PSZ pszName, ULONG flStyle,
                                          LONG x, LONG y, LONG cx, LONG cy, HWND hwndOwner,
                                          HWND hwndInsertBehind, ULONG id, PVOID pCtlData,
                                          PVOID pPresParams);

    OS2EXPORT HWND OS2API WinQueryDesktopWindow(HAB hab, HDC dc);

    OS2EXPORT BOOL OS2API WinQueryWindowProcess(HWND window, PULONG process, PULONG thread);

    OS2EXPORT BOOL OS2API WinIsWindow(HAB hab, HWND hwnd);

    OS2EXPORT BOOL OS2API WinIsWindowEnabled(HWND hwnd);

    OS2EXPORT BOOL OS2API WinIsWindowVisible(HWND hwnd);

    OS2EXPORT BOOL OS2API WinInvalidateRegion(HWND a0, ULONG a1, BOOL a2);

    OS2EXPORT LONG OS2API WinLoadMessage(HAB a0, HMODULE a1, ULONG a2, LONG a3, PSZ a4);

    OS2EXPORT HWND OS2API WinQueryObjectWindow(HWND a0);

    OS2EXPORT BOOL OS2API WinSetMultWindowPos(HAB a0, PSWP a1, ULONG a2);

    /* INCL_WINWINDOWMGR */
    /* Window words */
    enum
    {
        QWL_STYLE = -2,
        QWS_ID = -1,
        QWS_XRESTORE = 0x000c,
        QWS_YRESTORE = 0x000e,
        QWS_CXRESTORE = 0x0010,
        QWS_CYRESTORE = 0x0012
    };

    typedef HANDLE HENUM;

    /* Window word indexes */
    enum
    {
        QWS_USER = 0,
        QWL_USER = 0,
        QWP_PFNWP = -3,
        QWL_HMQ = -4,
        QWS_FLAGS = 0x0008,
        QWS_RESULT = 0x000a,
        QWS_XMINIMIZE = 0x0014,
        QWS_YMINIMIZE = 0x0016,
        QWL_HHEAP = 0x0004,
        QWL_HWNDFOCUSSAVE = 0x0018,
        QWL_DEFBUTTON = 0x0040,
        QWL_PSSCBLK = 0x0048,
        QWL_PFEPBLK = 0x004c,
        QWL_PSTATBLK = 0x0050
    };

    typedef struct
    {
        PVOID pPresParams, pCtlData;
        ULONG id;
        HWND hwndInsertBehind, hwndOwner;
        LONG cy, cx, y, x;
        ULONG flStyle;
        PSZ pszText, pszClass;
        HWND hwndParent;
    } CREATESTRUCT, *PCREATESTRUCT;

    typedef struct
    {
        ULONG flClassStyle;
        PFNWP pfnWindowProc;
        ULONG cbWindowData;
    } CLASSINFO;

    typedef CLASSINFO *PCLASSINFO;

    OS2EXPORT BOOL OS2API WinSetActiveWindow(HWND desktop, HWND window);

    OS2EXPORT HWND OS2API WinQueryActiveWindow(HWND desktop);

    OS2EXPORT BOOL OS2API WinMapWindowPoints(HWND from, HWND to, PPOINTL points, LONG count);

    OS2EXPORT BOOL OS2API WinQueryUpdateRect(HWND window, PRECTL rectangle);

    OS2EXPORT HWND OS2API WinWindowFromDC(HDC dc);

    OS2EXPORT BOOL OS2API WinQueryClassInfo(HAB hab, PSZ name, PCLASSINFO info);

    OS2EXPORT BOOL OS2API WinQueryWindowRect(HWND hwnd, PRECTL rect);

    OS2EXPORT HWND OS2API WinWindowFromPoint(HWND parent, PPOINTL point, BOOL children);

    OS2EXPORT BOOL OS2API WinShowWindow(HWND window, BOOL show);

    OS2EXPORT HENUM OS2API WinBeginEnumWindows(HWND parent);

    OS2EXPORT HWND OS2API WinGetNextWindow(HENUM enumeration);

    OS2EXPORT BOOL OS2API WinEndEnumWindows(HENUM enumeration);

    OS2EXPORT HAB OS2API WinQueryAnchorBlock(HWND window);

    OS2EXPORT LONG OS2API WinQueryClassName(HWND window, LONG capacity, PCHAR output);

    OS2EXPORT ULONG OS2API WinQueryWindowULong(HWND window, LONG index);

    OS2EXPORT USHORT OS2API WinQueryWindowUShort(HWND window, LONG index);

    OS2EXPORT BOOL OS2API WinSetWindowUShort(HWND window, LONG index, USHORT value);

    OS2EXPORT BOOL OS2API WinSetWindowULong(HWND window, LONG index, ULONG value);

    OS2EXPORT PVOID OS2API WinQueryWindowPtr(HWND window, LONG index);

    OS2EXPORT BOOL OS2API WinSetWindowPtr(HWND window, LONG index, PVOID value);

    OS2EXPORT HPS OS2API WinGetPS(HWND window);

    OS2EXPORT HPS OS2API WinGetScreenPS(HWND desktop);

    OS2EXPORT BOOL OS2API WinReleasePS(HPS space);

    OS2EXPORT PFNWP OS2API WinSubclassWindow(HWND window, PFNWP procedure);

    OS2EXPORT HDC OS2API WinQueryWindowDC(HWND hwnd);

    OS2EXPORT HDC OS2API WinOpenWindowDC(HWND hwnd);

    OS2EXPORT HAB OS2API WinInitialize(ULONG flOptions);

    OS2EXPORT BOOL OS2API WinTerminate(HAB hab);

    OS2EXPORT BOOL OS2API WinDestroyWindow(HWND hwnd);

    OS2EXPORT BOOL OS2API WinRegisterClass(HAB hab, PSZ pszClassName, PFNWP pfnWndProc,
                                           ULONG flStyle, ULONG cbWindowData);

    OS2EXPORT MRESULT OS2API WinDefWindowProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);

    OS2EXPORT HPS OS2API WinBeginPaint(HWND hwnd, HPS hps, PRECTL prclPaint);

    OS2EXPORT BOOL OS2API WinEndPaint(HPS hps);

    OS2EXPORT BOOL OS2API WinFillRect(HPS hps, PRECTL prcl, LONG lColor);

    OS2EXPORT LONG OS2API WinScrollWindow(HWND window, LONG dx, LONG dy, PRECTL scroll, PRECTL clip,
                                          ULONG region, PRECTL update, ULONG flags);

    OS2EXPORT BOOL OS2API WinIsWindowShowing(HWND a0);

    OS2EXPORT LONG OS2API WinQueryUpdateRegion(HWND a0, ULONG a1);

    OS2EXPORT BOOL OS2API WinSetWindowBits(HWND a0, LONG a1, ULONG a2, ULONG a3);

    OS2EXPORT BOOL OS2API WinValidateRect(HWND a0, PRECTL a1, BOOL a2);

    OS2EXPORT BOOL OS2API WinValidateRegion(HWND a0, ULONG a1, BOOL a2);

    /* INCL_WINMESSAGEMGR */
    /* WinBroadcastMsg flags */
    enum
    {
        BMSG_SEND = 0x0001,
        BMSG_FRAMEONLY = 0x0008
    };

    typedef struct
    {
        HWND hwnd;
        ULONG msg;
        MPARAM mp1;
        MPARAM mp2;
        ULONG time;
        POINTL ptl;
        ULONG reserved;
    } QMSG, *PQMSG;

    // Window messages...

    enum
    {
        WM_NULL = 0x0000,
        WM_CREATE = 0x0001,
        WM_DESTROY = 0x0002,
        WM_ENABLE = 0x0004,
        WM_SHOW = 0x0005,
        WM_MOVE = 0x0006,
        WM_SIZE = 0x0007,
        WM_ADJUSTWINDOWPOS = 0x0008,
        WM_CALCVALIDRECTS = 0x0009,
        WM_SETWINDOWPARAMS = 0x000a,
        WM_QUERYWINDOWPARAMS = 0x000b,
        WM_HITTEST = 0x000c,
        WM_ACTIVATE = 0x000d,
        WM_SETFOCUS = 0x000f,
        WM_SETSELECTION = 0x0010,
        WM_PPAINT = 0x0011,
        WM_PSETFOCUS = 0x0012,
        WM_PSYSCOLORCHANGE = 0x0013,
        WM_PSIZE = 0x0014,
        WM_PACTIVATE = 0x0015,
        WM_PCONTROL = 0x0016,
        WM_COMMAND = 0x0020,
        WM_SYSCOMMAND = 0x0021,
        WM_HELP = 0x0022,
        WM_PAINT = 0x0023,
        WM_TIMER = 0x0024,
        WM_SEM1 = 0x0025,
        WM_SEM2 = 0x0026,
        WM_SEM3 = 0x0027,
        WM_SEM4 = 0x0028,
        WM_CLOSE = 0x0029,
        WM_QUIT = 0x002a,
        WM_SYSCOLORCHANGE = 0x002b,
        WM_SYSVALUECHANGED = 0x002d,
        WM_APPTERMINATENOTIFY = 0x002e,
        WM_PRESPARAMCHANGED = 0x002f,
        WM_CONTROL = 0x0030,
        WM_VSCROLL = 0x0031,
        WM_HSCROLL = 0x0032,
        WM_INITMENU = 0x0033,
        WM_MENUSELECT = 0x0034,
        WM_MENUEND = 0x0035,
        WM_DRAWITEM = 0x0036,
        WM_MEASUREITEM = 0x0037,
        WM_CONTROLPOINTER = 0x0038,
        WM_QUERYDLGCODE = 0x003a,
        WM_INITDLG = 0x003b,
        WM_SUBSTITUTESTRING = 0x003c,
        WM_MATCHMNEMONIC = 0x003d,
        WM_SAVEAPPLICATION = 0x003e,
        WM_FLASHWINDOW = 0x0040,
        WM_FORMATFRAME = 0x0041,
        WM_UPDATEFRAME = 0x0042,
        WM_FOCUSCHANGE = 0x0043,
        WM_SETBORDERSIZE = 0x0044,
        WM_TRACKFRAME = 0x0045,
        WM_MINMAXFRAME = 0x0046,
        WM_SETICON = 0x0047,
        WM_QUERYICON = 0x0048,
        WM_SETACCELTABLE = 0x0049,
        WM_QUERYACCELTABLE = 0x004a,
        WM_TRANSLATEACCEL = 0x004b,
        WM_QUERYTRACKINFO = 0x004c,
        WM_QUERYBORDERSIZE = 0x004d,
        WM_NEXTMENU = 0x004e,
        WM_ERASEBACKGROUND = 0x004f,
        WM_QUERYFRAMEINFO = 0x0050,
        WM_QUERYFOCUSCHAIN = 0x0051,
        WM_OWNERPOSCHANGE = 0x0052,
        WM_CALCFRAMERECT = 0x0053,
        WM_WINDOWPOSCHANGED = 0x0055,
        WM_ADJUSTFRAMEPOS = 0x0056,
        WM_QUERYFRAMECTLCOUNT = 0x0059,
        WM_QUERYHELPINFO = 0x005B,
        WM_SETHELPINFO = 0x005C,
        WM_ERROR = 0x005D,
        WM_REALIZEPALETTE = 0x005E,
        WM_RENDERFMT = 0x0060,
        WM_RENDERALLFMTS = 0x0061,
        WM_DESTROYCLIPBOARD = 0x0062,
        WM_PAINTCLIPBOARD = 0x0063,
        WM_SIZECLIPBOARD = 0x0064,
        WM_HSCROLLCLIPBOARD = 0x0065,
        WM_VSCROLLCLIPBOARD = 0x0066,
        WM_DRAWCLIPBOARD = 0x0067,
        WM_MOUSEMOVE = 0x0070,
        WM_BUTTON1DOWN = 0x0071,
        WM_BUTTON1UP = 0x0072,
        WM_BUTTON1DBLCLK = 0x0073,
        WM_BUTTON2DOWN = 0x0074,
        WM_BUTTON2UP = 0x0075,
        WM_BUTTON2DBLCLK = 0x0076,
        WM_BUTTON3DOWN = 0x0077,
        WM_BUTTON3UP = 0x0078,
        WM_BUTTON3DBLCLK = 0x0079,
        WM_CHAR = 0x007a,
        WM_VIOCHAR = 0x007b,
        WM_JOURNALNOTIFY = 0x007c,
        WM_MOUSEMAP = 0x007D,
        WM_VRNDISABLED = 0x007e,
        WM_VRNENABLED = 0x007f,
        WM_DDE_INITIATE = 0x00A0,
        WM_DDE_REQUEST = 0x00A1,
        WM_DDE_ACK = 0x00A2,
        WM_DDE_DATA = 0x00A3,
        WM_DDE_ADVISE = 0x00A4,
        WM_DDE_UNADVISE = 0x00A5,
        WM_DDE_POKE = 0x00A6,
        WM_DDE_EXECUTE = 0x00A7,
        WM_DDE_TERMINATE = 0x00A8,
        WM_DDE_INITIATEACK = 0x00A9,
        WM_QUERYCONVERTPOS = 0x00b0,
        WM_MSGBOXINIT = 0x010E,
        WM_MSGBOXDISMISS = 0x010F,
        WM_CTLCOLORCHANGE = 0x0129,
        WM_QUERYCTLTYPE = 0x0130,
        WM_CHORD = 0x0410,
        WM_BUTTON1MOTIONSTART = 0x0411,
        WM_BUTTON1MOTIONEND = 0x0412,
        WM_BUTTON1CLICK = 0x0413,
        WM_BUTTON2MOTIONSTART = 0x0414,
        WM_BUTTON2MOTIONEND = 0x0415,
        WM_BUTTON2CLICK = 0x0416,
        WM_BUTTON3MOTIONSTART = 0x0417,
        WM_BUTTON3MOTIONEND = 0x0418,
        WM_BUTTON3CLICK = 0x0419,
        WM_BEGINDRAG = 0x0420,
        WM_ENDDRAG = 0x0421,
        WM_SINGLESELECT = 0x0422,
        WM_OPEN = 0x0423,
        WM_CONTEXTMENU = 0x0424,
        WM_CONTEXTHELP = 0x0425,
        WM_TEXTEDIT = 0x0426,
        WM_BEGINSELECT = 0x0427,
        WM_ENDSELECT = 0x0428,
        WM_PICKUP = 0x0429,
        WM_SEMANTICEVENT = 0x0490,
        WM_USER = 0x1000,
    };

    // used by WM_HITTEST
    enum
    {
        HT_NORMAL = 0,
        HT_TRANSPARENT = -1,
        HT_DISCARD = -2,
        HT_ERROR = -3
    };

    /* WinPeekMsg options */
    enum
    {
        PM_NOREMOVE = 0x0000,
        PM_REMOVE = 0x0001
    };

    OS2EXPORT BOOL OS2API WinPeekMsg(HAB hab, PQMSG message, HWND filter, ULONG first, ULONG last,
                                     ULONG flags);

    OS2EXPORT APIRET OS2API WinWaitEventSem(HEV handle, ULONG timeout);

    OS2EXPORT APIRET OS2API WinRequestMutexSem(HMTX handle, ULONG timeout);

    OS2EXPORT APIRET OS2API WinWaitMuxWaitSem(HMUX handle, ULONG timeout, PULONG user);

    OS2EXPORT HMQ OS2API WinCreateMsgQueue(HAB hab, LONG cmsg);

    OS2EXPORT BOOL OS2API WinGetMsg(HAB hab, PQMSG pqmsg, HWND hwndFilter, ULONG msgFilterFirst,
                                    ULONG msgFilterLast);

    OS2EXPORT MRESULT OS2API WinDispatchMsg(HAB hab, PQMSG pqmsg);

    OS2EXPORT BOOL OS2API WinDestroyMsgQueue(HMQ hmq);

    OS2EXPORT MRESULT OS2API WinSendMsg(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);

    OS2EXPORT BOOL OS2API WinPostMsg(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);

    OS2EXPORT BOOL OS2API WinPostQueueMsg(HMQ hmq, ULONG msg, MPARAM mp1, MPARAM mp2);

    OS2EXPORT BOOL OS2API WinCancelShutdown(HMQ a0, BOOL a1);

    OS2EXPORT BOOL OS2API WinQueryMsgPos(HAB a0, PPOINTL a1);

    OS2EXPORT BOOL OS2API WinQueryQueueInfo(HMQ a0, PVOID a1, ULONG a2);

    OS2EXPORT BOOL OS2API WinBroadcastMsg(HWND a0, ULONG a1, MPARAM a2, MPARAM a3, ULONG a4);

    /* WinBroadcastMsg options */
    enum
    {
        BMSG_POST = 0x0000,
        BMSG_POSTQUEUE = 0x0002,
        BMSG_DESCENDANTS = 0x0004,
    };

    /* WM_COMMAND source codes */
    enum
    {
        CMDSRC_PUSHBUTTON = 1,
        CMDSRC_MENU = 2,
        CMDSRC_ACCELERATOR = 3,
        CMDSRC_FONTDLG = 4,
        CMDSRC_FILEDLG = 5,
        CMDSRC_PRINTDLG = 6,
        CMDSRC_COLORDLG = 7,
        CMDSRC_OTHER = 0
    };

    /* INCL_WININPUT */
    /* Virtual keys */
    enum
    {
        VK_BUTTON1 = 0x01,
        VK_BUTTON2 = 0x02,
        VK_BUTTON3 = 0x03,
        VK_SHIFT = 0x09,
        VK_CTRL = 0x0A
    };

    OS2EXPORT BOOL OS2API WinSetFocus(HWND desktop, HWND window);

    OS2EXPORT HWND OS2API WinQueryFocus(HWND desktop);

    OS2EXPORT BOOL OS2API WinSetCapture(HWND desktop, HWND window);

    OS2EXPORT HWND OS2API WinQueryCapture(HWND desktop);

    OS2EXPORT LONG OS2API WinGetKeyState(HWND desktop, LONG key);

    OS2EXPORT LONG OS2API WinGetPhysKeyState(HWND desktop, LONG scan);

    OS2EXPORT BOOL OS2API WinFocusChange(HWND a0, HWND a1, ULONG a2);

    OS2EXPORT BOOL OS2API WinSetKeyboardStateTable(HWND a0, PBYTE a1, BOOL a2);

    /* WinFocusChange flags */
    enum
    {
        FC_NOSETFOCUS = 0x0001,
        FC_NOBRINGTOTOP = FC_NOSETFOCUS,
        FC_NOLOSEFOCUS = 0x0002,
        FC_NOBRINGTOPFIRSTWINDOW = FC_NOLOSEFOCUS,
        FC_NOSETACTIVE = 0x0004,
        FC_NOLOSEACTIVE = 0x0008,
        FC_NOSETSELECTION = 0x0010,
        FC_NOLOSESELECTION = 0x0020
    };

    /* WM_CHAR key flags */
    enum
    {
        KC_NONE = 0x0000,
        KC_CHAR = 0x0001,
        KC_VIRTUALKEY = 0x0002,
        KC_SCANCODE = 0x0004,
        KC_SHIFT = 0x0008,
        KC_CTRL = 0x0010,
        KC_ALT = 0x0020,
        KC_KEYUP = 0x0040,
        KC_PREVDOWN = 0x0080,
        KC_LONEKEY = 0x0100,
        KC_DEADKEY = 0x0200,
        KC_COMPOSITE = 0x0400,
        KC_INVALIDCOMP = 0x0800,
        KC_TOGGLE = 0x1000,
        KC_INVALIDCHAR = 0x2000,
        KC_DBCSRSRVD1 = 0x4000,
        KC_DBCSRSRVD2 = 0x8000
    };

    /* Virtual key codes */
    enum
    {
        VK_BREAK = 0x04,
        VK_BACKSPACE = 0x05,
        VK_TAB = 0x06,
        VK_BACKTAB = 0x07,
        VK_NEWLINE = 0x08,
        VK_ALT = 0x0B,
        VK_ALTGRAF = 0x0C,
        VK_PAUSE = 0x0D,
        VK_CAPSLOCK = 0x0E,
        VK_ESC = 0x0F,
        VK_SPACE = 0x10,
        VK_PAGEUP = 0x11,
        VK_PAGEDOWN = 0x12,
        VK_END = 0x13,
        VK_HOME = 0x14,
        VK_LEFT = 0x15,
        VK_UP = 0x16,
        VK_RIGHT = 0x17,
        VK_DOWN = 0x18,
        VK_PRINTSCRN = 0x19,
        VK_INSERT = 0x1A,
        VK_DELETE = 0x1B,
        VK_SCRLLOCK = 0x1C,
        VK_NUMLOCK = 0x1D,
        VK_ENTER = 0x1E,
        VK_SYSRQ = 0x1F,
        VK_F1 = 0x20,
        VK_F2 = 0x21,
        VK_F3 = 0x22,
        VK_F4 = 0x23,
        VK_F5 = 0x24,
        VK_F6 = 0x25,
        VK_F7 = 0x26,
        VK_F8 = 0x27,
        VK_F9 = 0x28,
        VK_F10 = 0x29,
        VK_F11 = 0x2A,
        VK_F12 = 0x2B,
        VK_F13 = 0x2C,
        VK_F14 = 0x2D,
        VK_F15 = 0x2E,
        VK_F16 = 0x2F,
        VK_F17 = 0x30,
        VK_F18 = 0x31,
        VK_F19 = 0x32,
        VK_F20 = 0x33,
        VK_F21 = 0x34,
        VK_F22 = 0x35,
        VK_F23 = 0x36,
        VK_F24 = 0x37,
        VK_ENDDRAG = 0x38,
        VK_CLEAR = 0x39,
        VK_EREOF = 0x3A,
        VK_PA1 = 0x3B,
        VK_ATTN = 0x3C,
        VK_CRSEL = 0x3D,
        VK_EXSEL = 0x3E,
        VK_COPY = 0x3F,
        VK_BLK1 = 0x40,
        VK_BLK2 = 0x41,
        VK_MENU = VK_F10
    };

    /* INCL_WINDIALOGS */
    /* Message box styles */
    enum
    {
        MB_YESNO = 0x0004,
        MB_ICONQUESTION = 0x0010,
        MB_ICONHAND = 0x0040,
        MB_SYSTEMMODAL = 0x1000
    };

    /* Message box results */
    enum
    {
        MBID_YES = 6,
        MBID_ERROR = 0xffff
    };

    /* Dialog codes */
    enum
    {
        DID_OK = 1
    };

    enum
    {
        DID_CANCEL = 2,
        DID_ERROR = 0xffff
    };

    /* WM_QUERYDLGCODE results */
    enum
    {
        DLGC_ENTRYFIELD = 0x0001,
        DLGC_BUTTON = 0x0002,
        DLGC_RADIOBUTTON = 0x0004,
        DLGC_STATIC = 0x0008,
        DLGC_DEFAULT = 0x0010,
        DLGC_PUSHBUTTON = 0x0020,
        DLGC_CHECKBOX = 0x0040,
        DLGC_SCROLLBAR = 0x0080,
        DLGC_MENU = 0x0100,
        DLGC_TABONCLICK = 0x0200,
        DLGC_MLE = 0x0400
    };

    /* WinAlarm types */
    enum
    {
        WA_WARNING = 0,
        WA_NOTE = 1,
        WA_ERROR = 2
    };

    /* WinMessageBox styles */
    enum
    {
        MB_OK = 0x0000,
        MB_OKCANCEL = 0x0001,
        MB_RETRYCANCEL = 0x0002,
        MB_ABORTRETRYIGNORE = 0x0003,
        MB_YESNOCANCEL = 0x0005,
        MB_CANCEL = 0x0006,
        MB_ENTER = 0x0007,
        MB_ENTERCANCEL = 0x0008,
        MB_NOICON = 0x0000,
        MB_CUANOTIFICATION = 0x0000,
        MB_ICONEXCLAMATION = 0x0020,
        MB_CUAWARNING = 0x0020,
        MB_ICONASTERISK = 0x0030,
        MB_CUACRITICAL = 0x0040,
        MB_QUERY = MB_ICONQUESTION,
        MB_WARNING = MB_CUAWARNING,
        MB_INFORMATION = MB_ICONASTERISK,
        MB_CRITICAL = MB_CUACRITICAL,
        MB_ERROR = MB_CRITICAL,
        MB_CUSTOMICON = 0x0080,
        MB_DEFBUTTON1 = 0x0000,
        MB_DEFBUTTON2 = 0x0100,
        MB_DEFBUTTON3 = 0x0200,
        MB_APPLMODAL = 0x0000,
        MB_HELP = 0x2000,
        MB_MOVEABLE = 0x4000,
        MB_NONMODAL = 0x8000
    };

    /* WinMessageBox return values */
    enum
    {
        MBID_OK = 1,
        MBID_CANCEL = 2,
        MBID_ABORT = 3,
        MBID_RETRY = 4,
        MBID_IGNORE = 5,
        MBID_NO = 7,
        MBID_HELP = 8,
        MBID_ENTER = 9,
    };

#pragma pack(push, 2)

    typedef struct
    {
        USHORT fsItemStatus, cChildren, cchClassName, offClassName, cchText, offText;
        ULONG flStyle;
        SHORT x, y, cx, cy;
        USHORT id, offPresParams, offCtlData;
    } DLGTITEM;

    typedef struct
    {
        USHORT cbTemplate, type, codepage, offadlgti, fsTemplateStatus, iItemFocus, coffPresParams;
        DLGTITEM adlgti[1];
    } DLGTEMPLATE, *PDLGTEMPLATE;

#pragma pack(pop)

    OS2EXPORT HWND OS2API WinCreateDlg(HWND parent, HWND owner, PFNWP procedure, PDLGTEMPLATE data,
                                       PVOID parameters);

    OS2EXPORT HWND OS2API WinLoadDlg(HWND parent, HWND owner, PFNWP procedure, HMODULE module,
                                     ULONG id, PVOID parameters);

    OS2EXPORT ULONG OS2API WinDlgBox(HWND parent, HWND owner, PFNWP procedure, HMODULE module,
                                     ULONG id, PVOID parameters);

    OS2EXPORT ULONG OS2API WinProcessDlg(HWND dialog);

    OS2EXPORT HWND OS2API WinEnumDlgItem(HWND dialog, HWND item, ULONG code);

    OS2EXPORT BOOL OS2API WinDismissDlg(HWND dialog, ULONG result);

    OS2EXPORT MRESULT OS2API WinDefDlgProc(HWND dialog, ULONG message, MPARAM first, MPARAM second);

    OS2EXPORT MRESULT OS2API WinSendDlgItemMsg(HWND dialog, ULONG id, ULONG message, MPARAM first,
                                               MPARAM second);

    OS2EXPORT BOOL OS2API WinMapDlgPoints(HWND dialog, PPOINTL points, ULONG count, BOOL to_pixels);

    OS2EXPORT LONG OS2API WinQueryDlgItemTextLength(HWND window, ULONG id);

    OS2EXPORT BOOL OS2API WinSetDlgItemText(HWND window, ULONG id, PSZ text);

    OS2EXPORT ULONG OS2API WinQueryDlgItemText(HWND window, ULONG id, LONG capacity, PCHAR output);

    OS2EXPORT BOOL OS2API WinAlarm(HWND desktop, ULONG type);

    OS2EXPORT ULONG OS2API WinMessageBox(HWND parent, HWND owner, PSZ text, PSZ caption, ULONG id,
                                         ULONG style);

    OS2EXPORT BOOL OS2API WinQueryDlgItemShort(HWND a0, ULONG a1, PSHORT a2, BOOL a3);

    OS2EXPORT BOOL OS2API WinSetDlgItemShort(HWND a0, ULONG a1, USHORT a2, BOOL a3);

    /* WinEnumDlgItem codes */
    enum
    {
        EDI_FIRSTTABITEM = 0,
        EDI_LASTTABITEM = 1,
        EDI_NEXTTABITEM = 2,
        EDI_PREVTABITEM = 3,
        EDI_FIRSTGROUPITEM = 4,
        EDI_LASTGROUPITEM = 5,
        EDI_NEXTGROUPITEM = 6,
        EDI_PREVGROUPITEM = 7
    };

    /* INCL_WINMENUS */

    /* Menu control styles */
    enum
    {
        MS_ACTIONBAR = 0x00000001,
        MS_TITLEBUTTON = 0x00000002,
        MS_VERTICALFLIP = 0x00000004,
        MS_CONDITIONALCASCADE = 0x00000040
    };

    /* Menu control messages */
    enum
    {
        MM_INSERTITEM = 0x0180,
        MM_DELETEITEM = 0x0181,
        MM_QUERYITEM = 0x0182,
        MM_SETITEM = 0x0183,
        MM_QUERYITEMCOUNT = 0x0184,
        MM_STARTMENUMODE = 0x0185,
        MM_ENDMENUMODE = 0x0186,
        MM_REMOVEITEM = 0x0188,
        MM_SELECTITEM = 0x0189,
        MM_QUERYSELITEMID = 0x018a,
        MM_QUERYITEMTEXT = 0x018b,
        MM_QUERYITEMTEXTLENGTH = 0x018c,
        MM_SETITEMHANDLE = 0x018d,
        MM_SETITEMTEXT = 0x018e,
        MM_ITEMPOSITIONFROMID = 0x018f,
        MM_ITEMIDFROMPOSITION = 0x0190,
        MM_QUERYITEMATTR = 0x0191,
        MM_SETITEMATTR = 0x0192,
        MM_ISITEMVALID = 0x0193,
        MM_QUERYITEMRECT = 0x0194,
        MM_QUERYDEFAULTITEMID = 0x0431,
        MM_SETDEFAULTITEMID = 0x0432
    };

    /* Menu item positions and errors */
    enum
    {
        MIT_END = -1,
        MIT_NONE = -1,
        MIT_MEMERROR = -1,
        MIT_ERROR = -1,
        MIT_FIRST = -2,
        MIT_LAST = -3
    };

    /* Menu item styles */
    enum
    {
        MIS_TEXT = 0x0001,
        MIS_BITMAP = 0x0002,
        MIS_SEPARATOR = 0x0004,
        MIS_OWNERDRAW = 0x0008,
        MIS_SUBMENU = 0x0010,
        MIS_MULTMENU = 0x0020,
        MIS_SYSCOMMAND = 0x0040,
        MIS_HELP = 0x0080,
        MIS_STATIC = 0x0100,
        MIS_BUTTONSEPARATOR = 0x0200,
        MIS_BREAK = 0x0400,
        MIS_BREAKSEPARATOR = 0x0800,
        MIS_GROUP = 0x1000,
        MIS_SINGLE = 0x2000
    };

    /* Menu item attributes */
    enum
    {
        MIA_NODISMISS = 0x0020,
        MIA_FRAMED = 0x1000,
        MIA_CHECKED = 0x2000,
        MIA_DISABLED = 0x4000,
        MIA_HILITED = 0x8000
    };

#pragma pack(push, 2)

    typedef struct
    {
        SHORT iPosition;
        USHORT afStyle, afAttribute, id;
        HWND hwndSubMenu;
        ULONG hItem;
    } MENUITEM;

#pragma pack(pop)

    typedef struct
    {
        HWND hwnd;
        HPS hps;
        ULONG fsState, fsAttribute, fsStateOld, fsAttributeOld;
        RECTL rclItem;
        LONG idItem;
        ULONG hItem;
    } OWNERITEM;

    OS2EXPORT HWND OS2API WinLoadMenu(HWND frame, HMODULE module, ULONG id);

    OS2EXPORT HWND OS2API WinCreateMenu(HWND parent, PVOID data);

    OS2EXPORT BOOL OS2API WinPopupMenu(HWND parent, HWND owner, HWND menu, LONG x, LONG y, LONG id,
                                       ULONG flags);

    /* INCL_WINFRAMEMGR */
    /* Frame creation flags */
    enum
    {
        FCF_SIZEBORDER = 0x00000008,
        FCF_MINBUTTON = 0x00000010,
        FCF_MAXBUTTON = 0x00000020
    };

    /* System commands */
    enum
    {
        SC_SIZE = 0x8000,
        SC_MOVE = 0x8001,
        SC_MINIMIZE = 0x8002,
        SC_MAXIMIZE = 0x8003,
        SC_CLOSE = 0x8004,
        SC_RESTORE = 0x8008
    };

    // Built-in window class specific data...

    // frame control IDs.
    enum
    {
        FID_SYSMENU = 0x8002,
        FID_TITLEBAR = 0x8003,
        FID_MINMAX = 0x8004,
        FID_MENU = 0x8005,
        FID_VERTSCROLL = 0x8006,
        FID_HORZSCROLL = 0x8007,
        FID_CLIENT = 0x8008,
        FID_DBE_APPSTAT = 0x8010,
        FID_DBE_KBDSTAT = 0x8011,
        FID_DBE_PECIC = 0x8012,
        FID_DBE_KKPOPUP = 0x8013
    };

    /* Frame styles */
    enum
    {
        FS_ICON = 0x00000001,
        FS_ACCELTABLE = 0x00000002,
        FS_SHELLPOSITION = 0x00000004,
        FS_TASKLIST = 0x00000008,
        FS_NOBYTEALIGN = 0x00000010,
        FS_NOMOVEWITHOWNER = 0x00000020,
        FS_SYSMODAL = 0x00000040,
        FS_DLGBORDER = 0x00000080,
        FS_BORDER = 0x00000100,
        FS_SCREENALIGN = 0x00000200,
        FS_MOUSEALIGN = 0x00000400,
        FS_SIZEBORDER = 0x00000800,
        FS_AUTOICON = 0x00001000,
        FS_DBE_APPSTAT = 0x00008000,
        FS_STANDARD = 0x0000000F
    };

    /* Frame creation flags */
    enum
    {
        FCF_TITLEBAR = 0x00000001,
        FCF_SYSMENU = 0x00000002,
        FCF_MENU = 0x00000004,
        FCF_MINMAX = 0x00000030,
        FCF_VERTSCROLL = 0x00000040,
        FCF_HORZSCROLL = 0x00000080,
        FCF_DLGBORDER = 0x00000100,
        FCF_BORDER = 0x00000200,
        FCF_SHELLPOSITION = 0x00000400,
        FCF_TASKLIST = 0x00000800,
        FCF_NOBYTEALIGN = 0x00001000,
        FCF_NOMOVEWITHOWNER = 0x00002000,
        FCF_ICON = 0x00004000,
        FCF_ACCELTABLE = 0x00008000,
        FCF_SYSMODAL = 0x00010000,
        FCF_SCREENALIGN = 0x00020000,
        FCF_MOUSEALIGN = 0x00040000,
        FCF_HIDEBUTTON = 0x01000000,
        FCF_HIDEMAX = 0x01000020,
        FCF_CLOSEBUTTON = 0x04000000,
        FCF_AUTOICON = 0x40000000,
        FCF_STANDARD = 0x0000CC3F
    };

    /* WM_SYSCOMMAND command values */
    enum
    {
        SC_NEXT = 0x8005,
        SC_APPMENU = 0x8006,
        SC_SYSMENU = 0x8007,
        SC_NEXTFRAME = 0x8009,
        SC_NEXTWINDOW = 0x8010,
        SC_TASKMANAGER = 0x8011,
        SC_HELPKEYS = 0x8012,
        SC_HELPINDEX = 0x8013,
        SC_HELPEXTENDED = 0x8014,
        SC_SWITCHPANELIDS = 0x8015,
        SC_DBE_FIRST = 0x8018,
        SC_DBE_LAST = 0x801F,
        SC_BEGINDRAG = 0x8020,
        SC_ENDDRAG = 0x8021,
        SC_SELECT = 0x8022,
        SC_OPEN = 0x8023,
        SC_CONTEXTMENU = 0x8024,
        SC_CONTEXTHELP = 0x8025,
        SC_TEXTEDIT = 0x8026,
        SC_BEGINSELECT = 0x8027,
        SC_ENDSELECT = 0x8028,
        SC_WINDOW = 0x8029,
        SC_HIDE = 0x802a
    };

    /* Frame window flags (QWS_FLAGS) */
    enum
    {
        FF_FLASHWINDOW = 0x0001,
        FF_ACTIVE = 0x0002,
        FF_FLASHHILITE = 0x0004,
        FF_OWNERHIDDEN = 0x0008,
        FF_DLGDISMISSED = 0x0010,
        FF_OWNERDISABLED = 0x0020,
        FF_SELECTED = 0x0040,
        FF_NOACTIVATESWP = 0x0080,
        FF_DIALOGBOX = 0x0100
    };

    /* WM_QUERYFRAMEINFO flags */
    enum
    {
        FI_FRAME = 0x00000001,
        FI_OWNERHIDE = 0x00000002,
        FI_ACTIVATEOK = 0x00000004,
        FI_NOMOVEWITHOWNER = 0x00000008
    };

/* Two-byte packing preserves the shared USE16/USE32 PM layout. */
#pragma pack(push, 2)

    typedef struct
    {
        USHORT cb;
        ULONG flCreateFlags;
        USHORT hmodResources;
        USHORT idResources;
    } FRAMECDATA, *PFRAMECDATA;

#pragma pack(pop)

    OS2EXPORT BOOL OS2API WinCreateFrameControls(HWND frame, PFRAMECDATA data, PSZ title);

    OS2EXPORT BOOL OS2API WinCalcFrameRect(HWND frame, PRECTL rect, BOOL client);

    OS2EXPORT HWND OS2API WinCreateStdWindow(HWND hwndParent, ULONG flStyle, PULONG pflCreateFlags,
                                             PSZ pszClientClass, PSZ pszTitle, ULONG styleClient,
                                             HMODULE hmod, ULONG idResources, PHWND phwndClient);

    OS2EXPORT BOOL OS2API WinFlashWindow(HWND a0, BOOL a1);

    OS2EXPORT BOOL OS2API WinGetMaxPosition(HWND a0, PSWP a1);

    OS2EXPORT BOOL OS2API WinGetMinPosition(HWND a0, PSWP a1, PPOINTL a2);

    /* INCL_WINRECTANGLES */

    OS2EXPORT BOOL OS2API WinIntersectRect(HAB hab, PRECTL dst, PRECTL a, PRECTL b);

    OS2EXPORT BOOL OS2API WinUnionRect(HAB hab, PRECTL dst, PRECTL a, PRECTL b);

    OS2EXPORT BOOL OS2API WinCopyRect(HAB hab, PRECTL dst, PRECTL src);

    OS2EXPORT BOOL OS2API WinSetRect(HAB hab, PRECTL dst, LONG left, LONG bottom, LONG right,
                                     LONG top);

    OS2EXPORT BOOL OS2API WinSetRectEmpty(HAB hab, PRECTL dst);

    OS2EXPORT BOOL OS2API WinIsRectEmpty(HAB hab, PRECTL rect);

    OS2EXPORT BOOL OS2API WinEqualRect(HAB hab, PRECTL a, PRECTL b);

    OS2EXPORT BOOL OS2API WinOffsetRect(HAB hab, PRECTL rect, LONG dx, LONG dy);

    OS2EXPORT BOOL OS2API WinInflateRect(HAB hab, PRECTL rect, LONG dx, LONG dy);

    OS2EXPORT BOOL OS2API WinPtInRect(HAB hab, PRECTL rect, PPOINTL point);

    /* INCL_WINSYS */
    /* System colours */
    enum
    {
        SYSCLR_DIALOGBACKGROUND = -34
    };

    OS2EXPORT BOOL OS2API WinSetPresParam(HWND window, ULONG id, ULONG size, PVOID data);

    OS2EXPORT ULONG OS2API WinQueryPresParam(HWND window, ULONG first, ULONG second, PULONG found,
                                             ULONG capacity, PVOID output, ULONG flags);

    OS2EXPORT BOOL OS2API WinRemovePresParam(HWND window, ULONG id);

    OS2EXPORT LONG OS2API WinQuerySysColor(HWND desktop, LONG index, LONG reserved);

    /* WinQuerySysValue indexes */

    enum
    {
        SV_SWAPBUTTON = 0,
        SV_DBLCLKTIME = 1,
        SV_CXDBLCLK = 2,
        SV_CYDBLCLK = 3,
        SV_CXSIZEBORDER = 4,
        SV_CYSIZEBORDER = 5,
        SV_ALARM = 6,
        SV_RESERVEDFIRST1 = 7,
        SV_RESERVEDLAST1 = 8,
        SV_CURSORRATE = 9,
        SV_FIRSTSCROLLRATE = 10,
        SV_SCROLLRATE = 11,
        SV_NUMBEREDLISTS = 12,
        SV_WARNINGFREQ = 13,
        SV_NOTEFREQ = 14,
        SV_ERRORFREQ = 15,
        SV_WARNINGDURATION = 16,
        SV_NOTEDURATION = 17,
        SV_ERRORDURATION = 18,
        SV_RESERVEDFIRST = 19,
        SV_RESERVEDLAST = 19,
        SV_CXSCREEN = 20,
        SV_CYSCREEN = 21,
        SV_CXVSCROLL = 22,
        SV_CYHSCROLL = 23,
        SV_CYVSCROLLARROW = 24,
        SV_CXHSCROLLARROW = 25,
        SV_CXBORDER = 26,
        SV_CYBORDER = 27,
        SV_CXDLGFRAME = 28,
        SV_CYDLGFRAME = 29,
        SV_CYTITLEBAR = 30,
        SV_CYVSLIDER = 31,
        SV_CXHSLIDER = 32,
        SV_CXMINMAXBUTTON = 33,
        SV_CYMINMAXBUTTON = 34,
        SV_CYMENU = 35,
        SV_CXFULLSCREEN = 36,
        SV_CYFULLSCREEN = 37,
        SV_CXICON = 38,
        SV_CYICON = 39,
        SV_CXPOINTER = 40,
        SV_CYPOINTER = 41,
        SV_DEBUG = 42,
        SV_CMOUSEBUTTONS = 43,
        SV_CPOINTERBUTTONS = 43,
        SV_POINTERLEVEL = 44,
        SV_CURSORLEVEL = 45,
        SV_TRACKRECTLEVEL = 46,
        SV_CTIMERS = 47,
        SV_MOUSEPRESENT = 48,
        SV_CXBYTEALIGN = 49,
        SV_CXALIGN = 49,
        SV_CYBYTEALIGN = 50,
        SV_CYALIGN = 50,
        SV_NOTRESERVED = 56,
        SV_EXTRAKEYBEEP = 57,
        SV_SETLIGHTS = 58,
        SV_INSERTMODE = 59,
        SV_MENUROLLDOWNDELAY = 64,
        SV_MENUROLLUPDELAY = 65,
        SV_ALTMNEMONIC = 66,
        SV_TASKLISTMOUSEACCESS = 67,
        SV_CXICONTEXTWIDTH = 68,
        SV_CICONTEXTLINES = 69,
        SV_CHORDTIME = 70,
        SV_CXCHORD = 71,
        SV_CYCHORD = 72,
        SV_CXMOTIONSTART = 73,
        SV_CYMOTIONSTART = 74,
        SV_BEGINDRAG = 75,
        SV_ENDDRAG = 76,
        SV_SINGLESELECT = 77,
        SV_OPEN = 78,
        SV_CONTEXTMENU = 79,
        SV_CONTEXTHELP = 80,
        SV_TEXTEDIT = 81,
        SV_BEGINSELECT = 82,
        SV_ENDSELECT = 83,
        SV_BEGINDRAGKB = 84,
        SV_ENDDRAGKB = 85,
        SV_SELECTKB = 86,
        SV_OPENKB = 87,
        SV_CONTEXTMENUKB = 88,
        SV_CONTEXTHELPKB = 89,
        SV_TEXTEDITKB = 90,
        SV_BEGINSELECTKB = 91,
        SV_ENDSELECTKB = 92,
        SV_ANIMATION = 93,
        SV_ANIMATIONSPEED = 94,
        SV_MONOICONS = 95,
        SV_KBDALTERED = 96,
        SV_PRINTSCREEN = 97,
        SV_LOCKSTARTINPUT = 98,
        SV_CSYSVALUES = 108
    };

    /* System colour indexes */
    enum
    {
        SYSCLR_SHADOWHILITEBGND = -50,
        SYSCLR_SHADOWHILITEFGND = -49,
        SYSCLR_SHADOWTEXT = -48,
        SYSCLR_ENTRYFIELD = -47,
        SYSCLR_MENUDISABLEDTEXT = -46,
        SYSCLR_MENUHILITE = -45,
        SYSCLR_MENUHILITEBGND = -44,
        SYSCLR_PAGEBACKGROUND = -43,
        SYSCLR_FIELDBACKGROUND = -42,
        SYSCLR_BUTTONLIGHT = -41,
        SYSCLR_BUTTONMIDDLE = -40,
        SYSCLR_BUTTONDARK = -39,
        SYSCLR_BUTTONDEFAULT = -38,
        SYSCLR_TITLEBOTTOM = -37,
        SYSCLR_SHADOW = -36,
        SYSCLR_ICONTEXT = -35,
        SYSCLR_HILITEFOREGROUND = -33,
        SYSCLR_HILITEBACKGROUND = -32,
        SYSCLR_INACTIVETITLETEXTBGND = -31,
        SYSCLR_ACTIVETITLETEXTBGND = -30,
        SYSCLR_INACTIVETITLETEXT = -29,
        SYSCLR_ACTIVETITLETEXT = -28,
        SYSCLR_OUTPUTTEXT = -27,
        SYSCLR_WINDOWSTATICTEXT = -26,
        SYSCLR_SCROLLBAR = -25,
        SYSCLR_BACKGROUND = -24,
        SYSCLR_ACTIVETITLE = -23,
        SYSCLR_INACTIVETITLE = -22,
        SYSCLR_MENU = -21,
        SYSCLR_WINDOW = -20,
        SYSCLR_WINDOWFRAME = -19,
        SYSCLR_MENUTEXT = -18,
        SYSCLR_WINDOWTEXT = -17,
        SYSCLR_TITLETEXT = -16,
        SYSCLR_ACTIVEBORDER = -15,
        SYSCLR_INACTIVEBORDER = -14,
        SYSCLR_APPWORKSPACE = -13,
        SYSCLR_HELPBACKGROUND = -12,
        SYSCLR_HELPTEXT = -11,
        SYSCLR_HELPHILITE = -10,
        SYSCLR_CSYSCOLORS = 41
    };

    /* Presentation parameter identifiers */
    enum
    {
        PP_FOREGROUNDCOLOR = 1,
        PP_FOREGROUNDCOLORINDEX = 2,
        PP_BACKGROUNDCOLOR = 3,
        PP_BACKGROUNDCOLORINDEX = 4,
        PP_HILITEFOREGROUNDCOLOR = 5,
        PP_HILITEFOREGROUNDCOLORINDEX = 6,
        PP_HILITEBACKGROUNDCOLOR = 7,
        PP_HILITEBACKGROUNDCOLORINDEX = 8,
        PP_DISABLEDFOREGROUNDCOLOR = 9,
        PP_DISABLEDFOREGROUNDCOLORINDEX = 10,
        PP_DISABLEDBACKGROUNDCOLOR = 11,
        PP_DISABLEDBACKGROUNDCOLORINDEX = 12,
        PP_BORDERCOLOR = 13,
        PP_BORDERCOLORINDEX = 14,
        PP_FONTNAMESIZE = 15,
        PP_FONTHANDLE = 16,
        PP_RESERVED = 17,
        PP_ACTIVECOLOR = 18,
        PP_ACTIVECOLORINDEX = 19,
        PP_INACTIVECOLOR = 20,
        PP_INACTIVECOLORINDEX = 21,
        PP_ACTIVETEXTFGNDCOLOR = 22,
        PP_ACTIVETEXTFGNDCOLORINDEX = 23,
        PP_ACTIVETEXTBGNDCOLOR = 24,
        PP_ACTIVETEXTBGNDCOLORINDEX = 25,
        PP_INACTIVETEXTFGNDCOLOR = 26,
        PP_INACTIVETEXTFGNDCOLORINDEX = 27,
        PP_INACTIVETEXTBGNDCOLOR = 28,
        PP_INACTIVETEXTBGNDCOLORINDEX = 29,
        PP_SHADOW = 30,
        PP_MENUFOREGROUNDCOLOR = 31,
        PP_MENUFOREGROUNDCOLORINDEX = 32,
        PP_MENUBACKGROUNDCOLOR = 33,
        PP_MENUBACKGROUNDCOLORINDEX = 34,
        PP_MENUHILITEFGNDCOLOR = 35,
        PP_MENUHILITEFGNDCOLORINDEX = 36,
        PP_MENUHILITEBGNDCOLOR = 37,
        PP_MENUHILITEBGNDCOLORINDEX = 38,
        PP_MENUDISABLEDFGNDCOLOR = 39,
        PP_MENUDISABLEDFGNDCOLORINDEX = 40,
        PP_MENUDISABLEDBGNDCOLOR = 41,
        PP_MENUDISABLEDBGNDCOLORINDEX = 42,
        PP_SHADOWTEXTCOLOR = 43,
        PP_SHADOWTEXTCOLORINDEX = 44,
        PP_SHADOWHILITEFGNDCOLOR = 45,
        PP_SHADOWHILITEFGNDCOLORINDEX = 46,
        PP_SHADOWHILITEBGNDCOLOR = 47,
        PP_SHADOWHILITEBGNDCOLORINDEX = 48,
        PP_ICONTEXTBACKGROUNDCOLOR = 49,
        PP_ICONTEXTBACKGROUNDCOLORINDEX = 50,
        PP_BORDERLIGHTCOLOR = 51,
        PP_BORDERDARKCOLOR = 52,
        PP_BORDER2COLOR = 53,
        PP_BORDER2LIGHTCOLOR = 54,
        PP_BORDER2DARKCOLOR = 55,
        PP_BORDERDEFAULTCOLOR = 56,
        PP_FIELDBACKGROUNDCOLOR = 57,
        PP_BUTTONBACKGROUNDCOLOR = 58,
        PP_BUTTONBORDERLIGHTCOLOR = 59,
        PP_BUTTONBORDERDARKCOLOR = 60,
        PP_ARROWCOLOR = 61,
        PP_ARROWBORDERLIGHTCOLOR = 62,
        PP_ARROWBORDERDARKCOLOR = 63,
        PP_ARROWDISABLEDCOLOR = 64,
        PP_CHECKLIGHTCOLOR = 65,
        PP_CHECKMIDDLECOLOR = 66,
        PP_CHECKDARKCOLOR = 67,
        PP_PAGEFOREGROUNDCOLOR = 68,
        PP_PAGEBACKGROUNDCOLOR = 69,
        PP_MAJORTABFOREGROUNDCOLOR = 70,
        PP_MAJORTABBACKGROUNDCOLOR = 71,
        PP_MINORTABFOREGROUNDCOLOR = 72,
        PP_MINORTABBACKGROUNDCOLOR = 73,
        PP_BIDI_FIRST = 0x0100,
        PP_BIDI_LAST = 0x012F,
        PP_USER = 0x8000
    };

    /* WinQueryPresParam options */
    enum
    {
        QPF_NOINHERIT = 0x0001,
        QPF_ID1COLORINDEX = 0x0002,
        QPF_ID2COLORINDEX = 0x0004,
        QPF_PURERGBCOLOR = 0x0008,
        QPF_VALIDFLAGS = 0x000F
    };

    OS2EXPORT LONG OS2API WinQuerySysValue(HWND desktop, LONG index);

    OS2EXPORT BOOL OS2API WinSetSysColors(HWND a0, ULONG a1, ULONG a2, LONG a3, ULONG a4, PLONG a5);

    OS2EXPORT BOOL OS2API WinSetSysValue(HWND a0, LONG a1, LONG a2);

    /* INCL_WINTIMER */

    /* Reserved timer identifiers */
    enum
    {
        TID_CURSOR = 0xffff,
        TID_SCROLL = 0xfffe,
        TID_FLASHWINDOW = 0xfffd,
        TID_USERMAX = 0x7fff
    };

    OS2EXPORT ULONG OS2API WinGetCurrentTime(HAB hab);

    OS2EXPORT ULONG OS2API WinStartTimer(HAB hab, HWND hwnd, ULONG id, ULONG milliseconds);

    OS2EXPORT BOOL OS2API WinStopTimer(HAB hab, HWND hwnd, ULONG id);

    /* INCL_WINACCELERATORS */

    /* Accelerator flags */
    enum
    {
        AF_CHAR = 0x0001,
        AF_VIRTUALKEY = 0x0002,
        AF_SCANCODE = 0x0004,
        AF_SHIFT = 0x0008,
        AF_CONTROL = 0x0010,
        AF_ALT = 0x0020,
        AF_LONEKEY = 0x0040,
        AF_SYSCOMMAND = 0x0100,
        AF_HELP = 0x0200
    };

    OS2EXPORT ULONG OS2API WinCreateAccelTable(HAB hab, PVOID data);

    OS2EXPORT ULONG OS2API WinLoadAccelTable(HAB hab, HMODULE module, ULONG id);

    OS2EXPORT ULONG OS2API WinCopyAccelTable(ULONG table, PVOID data, ULONG size);

    OS2EXPORT BOOL OS2API WinDestroyAccelTable(ULONG table);

    OS2EXPORT BOOL OS2API WinSetAccelTable(HAB hab, ULONG table, HWND frame);

    OS2EXPORT ULONG OS2API WinQueryAccelTable(HAB hab, HWND frame);

    OS2EXPORT BOOL OS2API WinTranslateAccel(HAB hab, HWND frame, ULONG table, PQMSG message);

    /* INCL_WINTRACKRECT */

    /* WinTrackRect flags */
    enum
    {
        TF_LEFT = 0x0001,
        TF_TOP = 0x0002,
        TF_RIGHT = 0x0004,
        TF_BOTTOM = 0x0008,
        TF_MOVE = 0x000F,
        TF_SETPOINTERPOS = 0x0010,
        TF_GRID = 0x0020,
        TF_STANDARD = 0x0040,
        TF_ALLINBOUNDARY = 0x0080,
        TF_VALIDATETRACKRECT = 0x0100,
        TF_PARTINBOUNDARY = 0x0200
    };

    // API entry points...
    typedef struct LXTRACKINFO
    {
        LONG cxBorder, cyBorder, cxGrid, cyGrid, cxKeyboard, cyKeyboard;
        RECTL rclTrack, rclBoundary;
        POINTL ptlMinTrackSize, ptlMaxTrackSize;
        ULONG fs;
    } LXTRACKINFO;

    OS2EXPORT BOOL OS2API WinTrackRect(HWND a0, HPS a1, LXTRACKINFO *a2);

    /* INCL_WINCLIPBOARD */

    /* Standard clipboard formats */
    enum
    {
        CF_TEXT = 1,
        CF_BITMAP = 2,
        CF_DSPTEXT = 3,
        CF_DSPBITMAP = 4,
        CF_METAFILE = 5,
        CF_DSPMETAFILE = 6,
        CF_PALETTE = 9,
        CF_MMPMFIRST = 10,
        CF_MMPMLAST = 19
    };

    /* Clipboard format information flags */
    enum
    {
        CFI_OWNERFREE = 0x0001,
        CFI_OWNERDISPLAY = 0x0002,
        CFI_POINTER = 0x0400,
        CFI_HANDLE = 0x0200
    };

    OS2EXPORT BOOL OS2API WinOpenClipbrd(HAB hab);

    OS2EXPORT BOOL OS2API WinCloseClipbrd(HAB hab);

    OS2EXPORT BOOL OS2API WinEmptyClipbrd(HAB hab);

    OS2EXPORT HWND OS2API WinQueryClipbrdOwner(HAB hab);

    OS2EXPORT BOOL OS2API WinSetClipbrdOwner(HAB hab, HWND owner);

    OS2EXPORT ULONG OS2API WinEnumClipbrdFmts(HAB hab, ULONG format);

    OS2EXPORT BOOL OS2API WinQueryClipbrdFmtInfo(HAB hab, ULONG format, PULONG flags);

    OS2EXPORT ULONG OS2API WinQueryClipbrdData(HAB hab, ULONG format);

    OS2EXPORT BOOL OS2API WinSetClipbrdData(HAB hab, ULONG data, ULONG format, ULONG flags);

    /* INCL_WINCURSORS */

    OS2EXPORT BOOL OS2API WinShowCursor(HWND window, BOOL show);

    /* INCL_WINPOINTERS */

    /* System pointer identifiers */
    enum
    {
        SPTR_ARROW = 1,
        SPTR_TEXT = 2,
        SPTR_WAIT = 3,
        SPTR_SIZE = 4,
        SPTR_MOVE = 5,
        SPTR_SIZENWSE = 6,
        SPTR_SIZENESW = 7,
        SPTR_SIZEWE = 8,
        SPTR_SIZENS = 9,
        SPTR_APPICON = 10,
        SPTR_ICONINFORMATION = 11,
        SPTR_ICONQUESTION = 12,
        SPTR_ICONERROR = 13,
        SPTR_ICONWARNING = 14,
        SPTR_ILLEGAL = 18,
        SPTR_FILE = 19,
        SPTR_FOLDER = 20,
        SPTR_MULTFILE = 21,
        SPTR_PROGRAM = 22,
        SPTR_DISPLAY_PTRS = 22,
        SPTR_PENFIRST = 23,
        SPTR_PENLAST = 39,
        SPTR_CPTR = 39,
        SPTR_HANDICON = SPTR_ICONERROR,
        SPTR_QUESICON = SPTR_ICONQUESTION,
        SPTR_BANGICON = SPTR_ICONWARNING,
        SPTR_NOTEICON = SPTR_ICONINFORMATION
    };

    /* WinDrawPointer flags */
    enum
    {
        DP_NORMAL = 0x0000,
        DP_HALFTONED = 0x0001,
        DP_INVERTED = 0x0002,
        DP_MINI = 0x0004
    };

    /* System bitmap identifiers */
    enum
    {
        SBMP_OLD_SYSMENU = 1,
        SBMP_OLD_SBUPARROW = 2,
        SBMP_OLD_SBDNARROW = 3,
        SBMP_OLD_SBRGARROW = 4,
        SBMP_OLD_SBLFARROW = 5,
        SBMP_MENUCHECK = 6,
        SBMP_OLD_CHECKBOXES = 7,
        SBMP_BTNCORNERS = 8,
        SBMP_OLD_MINBUTTON = 9,
        SBMP_OLD_MAXBUTTON = 10,
        SBMP_OLD_RESTOREBUTTON = 11,
        SBMP_OLD_CHILDSYSMENU = 12,
        SBMP_DRIVE = 15,
        SBMP_FILE = 16,
        SBMP_FOLDER = 17,
        SBMP_TREEPLUS = 18,
        SBMP_TREEMINUS = 19,
        SBMP_PROGRAM = 22,
        SBMP_MENUATTACHED = 23,
        SBMP_SIZEBOX = 24,
        SBMP_SYSMENU = 25,
        SBMP_MINBUTTON = 26,
        SBMP_MAXBUTTON = 27,
        SBMP_RESTOREBUTTON = 28,
        SBMP_CHILDSYSMENU = 29,
        SBMP_SYSMENUDEP = 30,
        SBMP_MINBUTTONDEP = 31,
        SBMP_MAXBUTTONDEP = 32,
        SBMP_RESTOREBUTTONDEP = 33,
        SBMP_CHILDSYSMENUDEP = 34,
        SBMP_SBUPARROW = 35,
        SBMP_SBDNARROW = 36,
        SBMP_SBLFARROW = 37,
        SBMP_SBRGARROW = 38,
        SBMP_SBUPARROWDEP = 39,
        SBMP_SBDNARROWDEP = 40,
        SBMP_SBLFARROWDEP = 41,
        SBMP_SBRGARROWDEP = 42,
        SBMP_SBUPARROWDIS = 43,
        SBMP_SBDNARROWDIS = 44,
        SBMP_SBLFARROWDIS = 45,
        SBMP_SBRGARROWDIS = 46,
        SBMP_COMBODOWN = 47,
        SBMP_CHECKBOXES = 48,
        SBMP_HIDE = 50,
        SBMP_HIDEDEP = 51,
        SBMP_CLOSE = 52,
        SBMP_CLOSEDEP = 53
    };

    typedef struct LXPOINTERINFO
    {
        ULONG fPointer;
        LONG xHotspot, yHotspot;
        ULONG hbmPointer, hbmColor, hbmMiniPointer, hbmMiniColor;
    } LXPOINTERINFO;

    OS2EXPORT BOOL OS2API WinSetPointerPos(HWND desktop, LONG x, LONG y);

    OS2EXPORT BOOL OS2API WinShowPointer(HWND desktop, BOOL show);

    OS2EXPORT BOOL OS2API WinQueryPointerPos(HWND hwnd, PPOINTL point);

    OS2EXPORT ULONG OS2API WinGetSysBitmap(HWND desktop, ULONG id);

    OS2EXPORT HPOINTER OS2API WinQuerySysPointer(HWND hwnd, LONG id, BOOL copy);

    OS2EXPORT HPOINTER OS2API WinLoadPointer(HWND desktop, HMODULE module, ULONG id);

    OS2EXPORT BOOL OS2API WinDrawPointer(HPS ps, LONG x, LONG y, HPOINTER pointer, ULONG flags);

    OS2EXPORT BOOL OS2API WinSetPointer(HWND desktop, HPOINTER pointer);

    OS2EXPORT BOOL OS2API WinDestroyPointer(HPOINTER pointer);

    OS2EXPORT HPOINTER OS2API WinQueryPointer(HWND desktop);

    OS2EXPORT BOOL OS2API WinQueryPointerInfo(HPOINTER a0, LXPOINTERINFO *a1);

    OS2EXPORT HPOINTER OS2API WinCreatePointerIndirect(HWND a0, LXPOINTERINFO *a1);

    /* INCL_WINHOOKS */

#define HMQ_CURRENT ((HMQ)1)

    /* Hook types */
    enum
    {
        HK_SENDMSG = 0,
        HK_INPUT = 1,
        HK_MSGFILTER = 2,
        HK_JOURNALRECORD = 3,
        HK_JOURNALPLAYBACK = 4,
        HK_HELP = 5,
        HK_LOADER = 6,
        HK_REGISTERUSERMSG = 7,
        HK_MSGCONTROL = 8,
        HK_PLIST_ENTRY = 9,
        HK_PLIST_EXIT = 10,
        HK_FINDWORD = 11,
        HK_CODEPAGECHANGED = 12,
        HK_WINDOWDC = 15,
        HK_DESTROYWINDOW = 16,
        HK_CHECKMSGFILTER = 20,
        HK_MSGINPUT = 21,
        HK_LOCKUP = 23,
        HK_FLUSHBUF = 24
    };

    OS2EXPORT BOOL OS2API WinSetHook(HAB hab, HMQ queue, LONG type, PFN procedure, HMODULE module);

    OS2EXPORT BOOL OS2API WinReleaseHook(HAB hab, HMQ queue, LONG type, PFN procedure,
                                         HMODULE module);

    /* INCL_WINCOUNTRY */

    /* WinCompareStrings results */
    enum
    {
        WCS_ERROR = 0,
        WCS_EQ = 1,
        WCS_LT = 2,
        WCS_GT = 3
    };

    OS2EXPORT ULONG OS2API WinUpper(HAB hab, ULONG codepage, ULONG country, PSZ text);

    OS2EXPORT ULONG OS2API WinUpperChar(HAB hab, ULONG codepage, ULONG country, ULONG character);

    OS2EXPORT UCHAR OS2API WinCpTranslateChar(HAB hab, ULONG source, UCHAR character,
                                              ULONG destination);

    OS2EXPORT ULONG OS2API WinQueryCp(HMQ queue);

    OS2EXPORT ULONG OS2API WinCompareStrings(HAB hab, ULONG codepage, ULONG country, PSZ first,
                                             PSZ second, ULONG reserved);

    OS2EXPORT BOOL OS2API WinCpTranslateString(HAB a0, ULONG a1, PSZ a2, ULONG a3, ULONG a4,
                                               PSZ a5);

    OS2EXPORT PSZ OS2API WinNextChar(HAB a0, ULONG a1, ULONG a2, PSZ a3);

    OS2EXPORT BOOL OS2API WinSetCp(HMQ a0, ULONG a1);

    /* INCL_WINATOM */

    OS2EXPORT ULONG OS2API WinCreateAtomTable(ULONG initial, ULONG buckets);

    OS2EXPORT ULONG OS2API WinDestroyAtomTable(ULONG table);

    OS2EXPORT ULONG OS2API WinQuerySystemAtomTable(VOID);

    OS2EXPORT ULONG OS2API WinAddAtom(ULONG table, PSZ name);

    OS2EXPORT ULONG OS2API WinFindAtom(ULONG table, PSZ name);

    OS2EXPORT ULONG OS2API WinDeleteAtom(ULONG table, ULONG atom);

    OS2EXPORT ULONG OS2API WinQueryAtomName(ULONG a0, ULONG a1, PSZ a2, ULONG a3);

    /* INCL_WINERRORS */

    typedef struct
    {
        ULONG cbFixedErrInfo;
        ERRORID idError;
        ULONG cDetailLevel;
        ULONG offaoffszMsg;
        ULONG offBinaryData;
    } ERRINFO, *PERRINFO;

    OS2EXPORT ERRORID OS2API WinGetLastError(HAB hab);

    OS2EXPORT BOOL OS2API WinFreeErrorInfo(PVOID a0);

    OS2EXPORT PVOID OS2API WinGetErrorInfo(HAB a0);

    /* INCL_WINLOAD */

    OS2EXPORT BOOL OS2API WinDeleteLibrary(HAB a0, ULONG a1);

    OS2EXPORT ULONG OS2API WinLoadLibrary(HAB a0, PSZ a1);

    /* INCL_WINPALETTE */

    OS2EXPORT LONG OS2API WinRealizePalette(HWND a0, HPS a1, PULONG a2);

#ifdef __cplusplus
}
#endif

/* As in the Toolkit, the window API brings in the shell, help and error
   declarations. */
#include "os2/include/pmerr.h"
#include "os2/include/pmsei.h"
#include "os2/include/pmshl.h"
#include "os2/include/pmhelp.h"

#endif
