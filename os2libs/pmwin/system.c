/* Public domain. No rights reserved. Wholly AI-authored. */
/* PM owns its palette and control metrics; screen dimensions come from the HAL display. */
#include "os2libs/pmwin/internal.h"

LONG WinQuerySysColor(HWND desktop, LONG index, LONG reserved)
{
    /* SYSCLR_SHADOWHILITEBGND (-50) through SYSCLR_HELPHILITE (-10). */
    static const LONG colors[] = {
        0x808080, /* SYSCLR_SHADOWHILITEBGND */
        0xffffff, /* SYSCLR_SHADOWHILITEFGND */
        0x808080, /* SYSCLR_SHADOWTEXT */
        0xffffff, /* SYSCLR_ENTRYFIELD */
        0x808080, /* SYSCLR_MENUDISABLEDTEXT */
        0x000000, /* SYSCLR_MENUHILITE */
        0xcccccc, /* SYSCLR_MENUHILITEBGND */
        0xffffff, /* SYSCLR_PAGEBACKGROUND */
        0xffffff, /* SYSCLR_FIELDBACKGROUND */
        0xffffff, /* SYSCLR_BUTTONLIGHT */
        0xcccccc, /* SYSCLR_BUTTONMIDDLE */
        0x808080, /* SYSCLR_BUTTONDARK */
        0x000000, /* SYSCLR_BUTTONDEFAULT */
        0x808080, /* SYSCLR_TITLEBOTTOM */
        0x808080, /* SYSCLR_SHADOW */
        0x000000, /* SYSCLR_ICONTEXT */
        0xcccccc, /* SYSCLR_DIALOGBACKGROUND */
        0xffffff, /* SYSCLR_HILITEFOREGROUND */
        0x000080, /* SYSCLR_HILITEBACKGROUND */
        0xc0c0c0, /* SYSCLR_INACTIVETITLETEXTBGND */
        0x408080, /* SYSCLR_ACTIVETITLETEXTBGND */
        0x000000, /* SYSCLR_INACTIVETITLETEXT */
        0xffffff, /* SYSCLR_ACTIVETITLETEXT */
        0x000000, /* SYSCLR_OUTPUTTEXT */
        0x000080, /* SYSCLR_WINDOWSTATICTEXT */
        0xc0c0c0, /* SYSCLR_SCROLLBAR */
        0x008080, /* SYSCLR_BACKGROUND */
        0x408080, /* SYSCLR_ACTIVETITLE */
        0xc0c0c0, /* SYSCLR_INACTIVETITLE */
        0xcccccc, /* SYSCLR_MENU */
        0xffffff, /* SYSCLR_WINDOW */
        0x808080, /* SYSCLR_WINDOWFRAME */
        0x000000, /* SYSCLR_MENUTEXT */
        0x000000, /* SYSCLR_WINDOWTEXT */
        0xffffff, /* SYSCLR_TITLETEXT */
        0xffff80, /* SYSCLR_ACTIVEBORDER */
        0xc0c0c0, /* SYSCLR_INACTIVEBORDER */
        0x808080, /* SYSCLR_APPWORKSPACE */
        0xffffff, /* SYSCLR_HELPBACKGROUND */
        0x000000, /* SYSCLR_HELPTEXT */
        0x000080, /* SYSCLR_HELPHILITE */
    };
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return 0;
    if (desktop != HWND_DESKTOP)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, 0);
    if (reserved || index < -50 || index > -10)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_PARAMETER_OUT_OF_RANGE, 0);
    return colors[index + 50];
}

LONG WinQuerySysValue(HWND desktop, LONG index)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return 0;
    if (desktop != HWND_DESKTOP)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, 0);
    /* Pixel metrics describe the controls provided by this PM implementation. */
    switch (index)
    {
        case 0:
            return 0; /* SV_SWAPBUTTON */
        case 1:
            return 500; /* SV_DBLCLKTIME */
        case 2:
        case 3:
            return 4; /* double-click rectangle */
        case 4:
        case 5:
            return 4; /* sizing borders */
        case 9:
            return 500; /* cursor blink */
        case 10:
            return 300; /* initial scroll delay */
        case 11:
            return 50; /* scroll repeat */
        case 20:
        case 36:
            return desktop_window.w;
        case 21:
            return desktop_window.h;
        case 22:
        case 23:
        case 24:
        case 25:
            return 16;
        case 26:
        case 27:
            return 1;
        case 28:
        case 29:
            return 4;
        case 30:
        case 34:
        case 35:
            return 20;
        case 31:
        case 32:
        case 33:
            return 16;
        case 37:
            return desktop_window.h > 20 ? desktop_window.h - 20 : 0;
        case 38:
        case 39:
        case 40:
        case 41:
            return 32;
        case 42:
            return 0; /* SV_DEBUG */
        case 43:
            return 3; /* pointer buttons */
        case 44:
        case 45:
        case 46:
            return 0;
        case 47:
            return 65535; /* timer IDs */
        case 48:
            return 1; /* pointer present */
        case 49:
        case 50:
            return 1;
        default:
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_PARAMETER_OUT_OF_RANGE, 0);
    }
}
