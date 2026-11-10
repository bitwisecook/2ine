/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows pixel metrics follow the active PM display and control geometry. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/pmwin.h"
#include "os2/include/pmgpi.h"
#include "os2/ps/awe/gdi/dc.h"
#include "emulator/abi.h"

/* PM's 12-byte BITMAPINFOHEADER: cbFix, then USHORT cx and cy. */
enum
{
    BITMAPINFOHEADER_CX = 4,
    BITMAPINFOHEADER_CY = 6,
    BITMAPINFOHEADER_SIZE = 12
};

LONG GetMenuCheckMarkDimensions(void)
{
    TRACE_NATIVE("GetMenuCheckMarkDimensions()");
    if (!open32Presentation())
        return 0;
    ULONG bitmap = WinGetSysBitmap(HWND_DESKTOP, SBMP_MENUCHECK);
    if (!bitmap)
        return 0;
    uint8 header[BITMAPINFOHEADER_SIZE] = {0};
    BOOL found = GpiQueryBitmapParameters(bitmap, header);
    GpiDeleteBitmap(bitmap);
    if (!found)
        return 0;
    /* Width in the low word, height in the high word. */
    return (LONG)((ULONG)lx_guest_load_u16(header + BITMAPINFOHEADER_CX) |
                  (ULONG)lx_guest_load_u16(header + BITMAPINFOHEADER_CY) << 16);
}

LONG GetSystemMetrics(LONG index)
{
    TRACE_NATIVE("GetSystemMetrics(%d)", index);
    if (!open32Presentation())
        return 0;
    /* Windows draws square scroll thumbs, so the thumb size is the arrow size. */
    static const LONG pm_metrics[] = {[WIN32_SM_CXSCREEN] = SV_CXSCREEN,
                                      [WIN32_SM_CYSCREEN] = SV_CYSCREEN,
                                      [WIN32_SM_CXVSCROLL] = SV_CXVSCROLL,
                                      [WIN32_SM_CYHSCROLL] = SV_CYHSCROLL,
                                      [WIN32_SM_CYCAPTION] = SV_CYTITLEBAR,
                                      [WIN32_SM_CXBORDER] = SV_CXBORDER,
                                      [WIN32_SM_CYBORDER] = SV_CYBORDER,
                                      [WIN32_SM_CXDLGFRAME] = SV_CXDLGFRAME,
                                      [WIN32_SM_CYDLGFRAME] = SV_CYDLGFRAME,
                                      [WIN32_SM_CYVTHUMB] = SV_CYVSCROLLARROW,
                                      [WIN32_SM_CXHTHUMB] = SV_CXHSCROLLARROW,
                                      [WIN32_SM_CXICON] = SV_CXICON,
                                      [WIN32_SM_CYICON] = SV_CYICON,
                                      [WIN32_SM_CXCURSOR] = SV_CXPOINTER,
                                      [WIN32_SM_CYCURSOR] = SV_CYPOINTER,
                                      [WIN32_SM_CYMENU] = SV_CYMENU,
                                      [WIN32_SM_CXFULLSCREEN] = SV_CXFULLSCREEN,
                                      [WIN32_SM_CYFULLSCREEN] = SV_CYFULLSCREEN};
    if (index >= 0 && (ULONG)index < sizeof pm_metrics / sizeof *pm_metrics)
        return WinQuerySysValue(HWND_DESKTOP, pm_metrics[index]);
    switch (index)
    {
        case WIN32_SM_MOUSEPRESENT:
            return WinQuerySysValue(HWND_DESKTOP, SV_MOUSEPRESENT);
        case WIN32_SM_SWAPBUTTON:
            return WinQuerySysValue(HWND_DESKTOP, SV_SWAPBUTTON);
        case WIN32_SM_CYVSCROLL:
            return WinQuerySysValue(HWND_DESKTOP, SV_CYVSCROLLARROW);
        case WIN32_SM_CXHSCROLL:
            return WinQuerySysValue(HWND_DESKTOP, SV_CXHSCROLLARROW);
        case WIN32_SM_CXSIZE:
            return WinQuerySysValue(HWND_DESKTOP, SV_CXMINMAXBUTTON);
        case WIN32_SM_CYSIZE:
            return WinQuerySysValue(HWND_DESKTOP, SV_CYMINMAXBUTTON);
        case WIN32_SM_CXFRAME:
            return WinQuerySysValue(HWND_DESKTOP, SV_CXSIZEBORDER);
        case WIN32_SM_CYFRAME:
            return WinQuerySysValue(HWND_DESKTOP, SV_CYSIZEBORDER);
        case WIN32_SM_CXDOUBLECLK:
            return WinQuerySysValue(HWND_DESKTOP, SV_CXDBLCLK);
        case WIN32_SM_CYDOUBLECLK:
            return WinQuerySysValue(HWND_DESKTOP, SV_CYDBLCLK);
        case WIN32_SM_CMOUSEBUTTONS:
            return WinQuerySysValue(HWND_DESKTOP, SV_CMOUSEBUTTONS);
        default:
            return 0;
    }
}

ULONG GetSysColor(LONG index)
{
    static const LONG colors[] = {[WIN32_COLOR_SCROLLBAR] = SYSCLR_SCROLLBAR,
                                  [WIN32_COLOR_BACKGROUND] = SYSCLR_BACKGROUND,
                                  [WIN32_COLOR_ACTIVECAPTION] = SYSCLR_ACTIVETITLE,
                                  [WIN32_COLOR_INACTIVECAPTION] = SYSCLR_INACTIVETITLE,
                                  [WIN32_COLOR_MENU] = SYSCLR_MENU,
                                  [WIN32_COLOR_WINDOW] = SYSCLR_WINDOW,
                                  [WIN32_COLOR_WINDOWFRAME] = SYSCLR_WINDOWFRAME,
                                  [WIN32_COLOR_MENUTEXT] = SYSCLR_MENUTEXT,
                                  [WIN32_COLOR_WINDOWTEXT] = SYSCLR_WINDOWTEXT,
                                  [WIN32_COLOR_CAPTIONTEXT] = SYSCLR_TITLETEXT,
                                  [WIN32_COLOR_ACTIVEBORDER] = SYSCLR_ACTIVEBORDER,
                                  [WIN32_COLOR_INACTIVEBORDER] = SYSCLR_INACTIVEBORDER,
                                  [WIN32_COLOR_APPWORKSPACE] = SYSCLR_APPWORKSPACE,
                                  [WIN32_COLOR_HIGHLIGHT] = SYSCLR_HILITEBACKGROUND,
                                  [WIN32_COLOR_HIGHLIGHTTEXT] = SYSCLR_HILITEFOREGROUND,
                                  [WIN32_COLOR_BTNFACE] = SYSCLR_BUTTONMIDDLE,
                                  [WIN32_COLOR_BTNSHADOW] = SYSCLR_BUTTONDARK,
                                  [WIN32_COLOR_GRAYTEXT] = SYSCLR_MENUDISABLEDTEXT,
                                  [WIN32_COLOR_BTNTEXT] = SYSCLR_MENUTEXT,
                                  [WIN32_COLOR_INACTIVECAPTIONTEXT] = SYSCLR_INACTIVETITLETEXT,
                                  [WIN32_COLOR_BTNHIGHLIGHT] = SYSCLR_BUTTONLIGHT};
    if (!open32Presentation())
        return 0;
    LONG pm_index = index < 0 ? index : index <= WIN32_COLOR_BTNHIGHLIGHT ? colors[index] : 0;
    ULONG rgb = (ULONG)WinQuerySysColor(HWND_DESKTOP, pm_index, 0);
    /* PM RGB is 0x00RRGGBB; a COLORREF is 0x00BBGGRR. */
    return (rgb & 255) << 16 | (rgb & 0xFF00) | (rgb >> 16 & 255);
}
