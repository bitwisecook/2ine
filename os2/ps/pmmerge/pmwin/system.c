/* Public domain. No rights reserved. Wholly AI-authored. */
/* PM owns its palette and control metrics; screen dimensions come from the host display. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"

static LONG changed_colors[41], changed_values[108];
static unsigned char color_changed[41], value_changed[108];

static LONG display_metric(LONG pixels)
{
    return lx_pm_display_scale(pixels);
}

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
        0x2b00aa, /* SYSCLR_ACTIVETITLETEXTBGND */
        0x000000, /* SYSCLR_INACTIVETITLETEXT */
        0xffffff, /* SYSCLR_ACTIVETITLETEXT */
        0x000000, /* SYSCLR_OUTPUTTEXT */
        0x000080, /* SYSCLR_WINDOWSTATICTEXT */
        0xc0c0c0, /* SYSCLR_SCROLLBAR */
        0x008080, /* SYSCLR_BACKGROUND */
        0x2b00aa, /* SYSCLR_ACTIVETITLE */
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
    return __atomic_load_n(&color_changed[index + 50], __ATOMIC_ACQUIRE)
               ? __atomic_load_n(&changed_colors[index + 50], __ATOMIC_RELAXED)
               : colors[index + 50];
}

LONG WinQuerySysValue(HWND desktop, LONG index)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return 0;
    if (desktop != HWND_DESKTOP)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, 0);
    if (index >= 0 && index < 108 && __atomic_load_n(&value_changed[index], __ATOMIC_ACQUIRE))
        return __atomic_load_n(&changed_values[index], __ATOMIC_RELAXED);
    /* Pixel metrics describe the controls provided by this PM implementation. */
    switch (index)
    {
        case 0:
            return 0; /* SV_SWAPBUTTON */
        case 1:
            return 500; /* SV_DBLCLKTIME */
        case 2:
        case 3:
            return display_metric(4); /* double-click rectangle */
        case 4:
        case 5:
            return display_metric(4); /* sizing borders */
        case 6:
            return 1; /* SV_ALARM */
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
            return display_metric(16);
        case 26:
        case 27:
            return display_metric(1);
        case 28:
        case 29:
            return display_metric(4);
        case 30:
        case 34:
        case 35:
            return display_metric(20);
        case 31:
        case 32:
        case 33:
            return display_metric(16);
        case 37:
            return desktop_window.h > display_metric(20) ? desktop_window.h - display_metric(20)
                                                         : 0;
        case 38:
        case 39:
        case 40:
        case 41:
            return display_metric(32);
        case 42:
            return 0; /* SV_DEBUG */
        case 43:
            return 3; /* pointer buttons */
        case 44:
            return queryPointerLevel(); /* SV_POINTERLEVEL */
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
        case 12:
        case 57:
        case 58:
        case 64:
        case 65:
        case 66:
        case 67:
        case 80: /* SV_CONTEXTHELP has no default mouse event. */
        case 84:
        case 85:
        case 86:
        case 87:
        case 88:
        case 89:
        case 90:
        case 91:
        case 92:
        case 93:
        case 94:
        case 95:
        case 96:
        case 97:
        case 98:
            return 0;
        case 13:
        case 14:
            return 440; /* warning and note alarm frequency */
        case 15:
            return 220; /* error alarm frequency */
        case 16:
        case 17:
        case 18:
            return 80; /* alarm durations */
        case 59:
            return 1; /* SV_INSERTMODE */
        case 68:
            return display_metric(32) * 2; /* SV_CXICONTEXTWIDTH */
        case 69:
            return 2; /* SV_CICONTEXTLINES */
        case 70:
            return 200; /* SV_CHORDTIME */
        case 71:
        case 72:
            return display_metric(4); /* chord rectangle */
        case 73:
        case 74:
            return display_metric(4); /* motion start distance */
        case 75:
            return WM_BUTTON2MOTIONSTART; /* SV_BEGINDRAG */
        case 76:
            return WM_BUTTON2UP; /* SV_ENDDRAG */
        case 77:
            return WM_BUTTON1CLICK; /* SV_SINGLESELECT */
        case 78:
            return WM_BUTTON1DBLCLK; /* SV_OPEN */
        case 79:
            return WM_BUTTON2CLICK; /* SV_CONTEXTMENU */
        case 81:
            return WM_BUTTON1CLICK | 0x20 << 16; /* SV_TEXTEDIT, with KC_ALT */
        case 82:
            return WM_BUTTON1MOTIONSTART; /* SV_BEGINSELECT */
        case 83:
            return WM_BUTTON1UP; /* SV_ENDSELECT */
        default:
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_PARAMETER_OUT_OF_RANGE, 0);
    }
}

BOOL WinSetSysColors(HWND desktop, ULONG options, ULONG format, LONG first, ULONG count,
                     PLONG input)
{
    if (desktop != HWND_DESKTOP || !getAnchorBlockNoHAB() || (options & ~5u) || format > 1 ||
        count > 82 || (count && !input) || (format && count % 2))
        return FALSE;
    if (!format && (first < -50 || (int64_t)first + count > -9))
        return FALSE;
    /* LCOL_RESET restores the initial colors before applying this request. */
    if (options & 1)
        for (unsigned i = 0; i < 41; i++)
            __atomic_store_n(&color_changed[i], 0, __ATOMIC_RELEASE);
    LONG indices[82], colors[82];
    ULONG n = format ? count / 2 : count;
    for (ULONG i = 0; i < n; i++)
    {
        LONG index = format ? input[i * 2] : first + i,
             color = format ? input[i * 2 + 1] : input[i];
        if (index < -50 || index > -10)
            return FALSE;
        if (color >= -50 && color <= -10)
            color = WinQuerySysColor(desktop, color, 0);
        if (color < 0 || color > 0xffffff)
            return FALSE;
        indices[i] = index + 50;
        colors[i] = color;
    }
    for (ULONG i = 0; i < n; i++)
    {
        __atomic_store_n(&changed_colors[indices[i]], colors[i], __ATOMIC_RELAXED);
        __atomic_store_n(&color_changed[indices[i]], 1, __ATOMIC_RELEASE);
    }
    WinBroadcastMsg(HWND_DESKTOP, 0x2b, 0, 0, 5);
    WinInvalidateRect(HWND_DESKTOP, NULL, TRUE);
    return TRUE;
}

BOOL WinSetSysValue(HWND desktop, LONG index, LONG value)
{
    if (desktop != HWND_DESKTOP || !getAnchorBlockNoHAB())
        return FALSE;
    /* Settable identities are 0-6, 9-18 and 57-107. */
    if (index < 0 || index > 107 || (index > 18 && index < 57) || index == 7 || index == 8)
    {
        WinSetErrorInfo(PMERR_PARAMETER_OUT_OF_RANGE, 0);
        return FALSE;
    }
    /* A sizing border never becomes narrower than the plain border. */
    if (index == 4 || index == 5)
    {
        LONG minimum = WinQuerySysValue(desktop, index + 22);
        if (value < minimum)
            value = minimum;
    }
    __atomic_store_n(&changed_values[index], value, __ATOMIC_RELAXED);
    __atomic_store_n(&value_changed[index], 1, __ATOMIC_RELEASE);
    return TRUE;
}
