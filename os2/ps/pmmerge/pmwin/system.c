/* Public domain. No rights reserved. Wholly AI-authored. */
/* PM owns its palette and control metrics; screen dimensions come from the host display. */
#include "os2/include/private/pmwinp.h"
#include "os2/include/private/pmgrep.h"

/* Colours are indexed from SYSCLR_SHADOWHILITEBGND, the lowest system colour index. */
static LONG changed_colors[SYSCLR_CSYSCOLORS], changed_values[SV_CSYSVALUES];
static unsigned char color_changed[SYSCLR_CSYSCOLORS], value_changed[SV_CSYSVALUES];

enum
{
    MAX_RGB = 0xffffff
};

static LONG display_metric(LONG pixels)
{
    return lx_pm_display_scale(pixels);
}

LONG WinQuerySysColor(HWND desktop, LONG index, LONG reserved)
{
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
    if (reserved || index < SYSCLR_SHADOWHILITEBGND || index > SYSCLR_HELPHILITE)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_PARAMETER_OUT_OF_RANGE, 0);
    LONG slot = index - SYSCLR_SHADOWHILITEBGND;
    return __atomic_load_n(&color_changed[slot], __ATOMIC_ACQUIRE)
               ? __atomic_load_n(&changed_colors[slot], __ATOMIC_RELAXED)
               : colors[slot];
}

LONG WinQuerySysValue(HWND desktop, LONG index)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor)
        return 0;
    if (desktop != HWND_DESKTOP)
        SET_WIN_ERROR_AND_RETURN(anchor, PMERR_INVALID_HWND, 0);
    if (index >= 0 && index < SV_CSYSVALUES &&
        __atomic_load_n(&value_changed[index], __ATOMIC_ACQUIRE))
        return __atomic_load_n(&changed_values[index], __ATOMIC_RELAXED);
    /* Pixel metrics describe the controls provided by this PM implementation. */
    switch (index)
    {
        case SV_SWAPBUTTON:
            return 0;
        case SV_DBLCLKTIME:
            return 500;
        case SV_CXDBLCLK:
        case SV_CYDBLCLK:
        case SV_CXSIZEBORDER:
        case SV_CYSIZEBORDER:
            return display_metric(4);
        case SV_ALARM:
            return 1;
        case SV_CURSORRATE:
            return 500;
        case SV_FIRSTSCROLLRATE:
            return 300;
        case SV_SCROLLRATE:
            return 50;
        case SV_CXSCREEN:
        case SV_CXFULLSCREEN:
            return desktop_window.w;
        case SV_CYSCREEN:
            return desktop_window.h;
        case SV_CXVSCROLL:
        case SV_CYHSCROLL:
        case SV_CYVSCROLLARROW:
        case SV_CXHSCROLLARROW:
            return display_metric(16);
        case SV_CXBORDER:
        case SV_CYBORDER:
            return display_metric(1);
        case SV_CXDLGFRAME:
        case SV_CYDLGFRAME:
            return display_metric(4);
        case SV_CYTITLEBAR:
        case SV_CYMINMAXBUTTON:
        case SV_CYMENU:
            return display_metric(20);
        case SV_CYVSLIDER:
        case SV_CXHSLIDER:
        case SV_CXMINMAXBUTTON:
            return display_metric(16);
        case SV_CYFULLSCREEN:
            return desktop_window.h > display_metric(20) ? desktop_window.h - display_metric(20)
                                                         : 0;
        case SV_CXICON:
        case SV_CYICON:
        case SV_CXPOINTER:
        case SV_CYPOINTER:
            return display_metric(32);
        case SV_DEBUG:
            return 0;
        case SV_CMOUSEBUTTONS:
            return 3;
        case SV_POINTERLEVEL:
            return queryPointerLevel();
        case SV_CURSORLEVEL:
        case SV_TRACKRECTLEVEL:
            return 0;
        case SV_CTIMERS:
            return 65535;
        case SV_MOUSEPRESENT:
        case SV_CXALIGN:
        case SV_CYALIGN:
            return 1;
        case SV_NUMBEREDLISTS:
        case SV_EXTRAKEYBEEP:
        case SV_SETLIGHTS:
        case SV_MENUROLLDOWNDELAY:
        case SV_MENUROLLUPDELAY:
        case SV_ALTMNEMONIC:
        case SV_TASKLISTMOUSEACCESS:
        case SV_CONTEXTHELP: /* No mouse event requests context help by default. */
        case SV_BEGINDRAGKB:
        case SV_ENDDRAGKB:
        case SV_SELECTKB:
        case SV_OPENKB:
        case SV_CONTEXTMENUKB:
        case SV_CONTEXTHELPKB:
        case SV_TEXTEDITKB:
        case SV_BEGINSELECTKB:
        case SV_ENDSELECTKB:
        case SV_ANIMATION:
        case SV_ANIMATIONSPEED:
        case SV_MONOICONS:
        case SV_KBDALTERED:
        case SV_PRINTSCREEN:
        case SV_LOCKSTARTINPUT:
            return 0;
        case SV_WARNINGFREQ:
        case SV_NOTEFREQ:
            return 440; /* Hz */
        case SV_ERRORFREQ:
            return 220; /* Hz */
        case SV_WARNINGDURATION:
        case SV_NOTEDURATION:
        case SV_ERRORDURATION:
            return 80; /* ms */
        case SV_INSERTMODE:
            return 1;
        case SV_CXICONTEXTWIDTH:
            return display_metric(32) * 2;
        case SV_CICONTEXTLINES:
            return 2;
        case SV_CHORDTIME:
            return 200;
        case SV_CXCHORD:
        case SV_CYCHORD:
        case SV_CXMOTIONSTART:
        case SV_CYMOTIONSTART:
            return display_metric(4);
        /* Mouse gestures pack the event message with its KC_* shift state in the high word. */
        case SV_BEGINDRAG:
            return WM_BUTTON2MOTIONSTART;
        case SV_ENDDRAG:
            return WM_BUTTON2UP;
        case SV_SINGLESELECT:
            return WM_BUTTON1CLICK;
        case SV_OPEN:
            return WM_BUTTON1DBLCLK;
        case SV_CONTEXTMENU:
            return WM_BUTTON2CLICK;
        case SV_TEXTEDIT:
            return WM_BUTTON1CLICK | KC_ALT << 16;
        case SV_BEGINSELECT:
            return WM_BUTTON1MOTIONSTART;
        case SV_ENDSELECT:
            return WM_BUTTON1UP;
        default:
            SET_WIN_ERROR_AND_RETURN(anchor, PMERR_PARAMETER_OUT_OF_RANGE, 0);
    }
}

BOOL WinSetSysColors(HWND desktop, ULONG options, ULONG format, LONG first, ULONG count,
                     PLONG input)
{
    if (desktop != HWND_DESKTOP || !getAnchorBlockNoHAB() ||
        (options & ~(ULONG)(LCOL_RESET | LCOL_PURECOLOR)) || format > LCOLF_INDRGB ||
        count > SYSCLR_CSYSCOLORS * 2 || (count && !input) || (format && count % 2))
        return FALSE;
    if (!format &&
        (first < SYSCLR_SHADOWHILITEBGND || (int64_t)first + count > SYSCLR_HELPHILITE + 1))
        return FALSE;
    /* LCOL_RESET restores the initial colors before applying this request. */
    if (options & LCOL_RESET)
        for (unsigned i = 0; i < SYSCLR_CSYSCOLORS; i++)
            __atomic_store_n(&color_changed[i], 0, __ATOMIC_RELEASE);
    LONG indices[SYSCLR_CSYSCOLORS * 2], colors[SYSCLR_CSYSCOLORS * 2];
    ULONG n = format ? count / 2 : count;
    for (ULONG i = 0; i < n; i++)
    {
        LONG index = format ? input[i * 2] : first + i,
             color = format ? input[i * 2 + 1] : input[i];
        if (index < SYSCLR_SHADOWHILITEBGND || index > SYSCLR_HELPHILITE)
            return FALSE;
        if (color >= SYSCLR_SHADOWHILITEBGND && color <= SYSCLR_HELPHILITE)
            color = WinQuerySysColor(desktop, color, 0);
        if (color < 0 || color > MAX_RGB)
            return FALSE;
        indices[i] = index - SYSCLR_SHADOWHILITEBGND;
        colors[i] = color;
    }
    for (ULONG i = 0; i < n; i++)
    {
        __atomic_store_n(&changed_colors[indices[i]], colors[i], __ATOMIC_RELAXED);
        __atomic_store_n(&color_changed[indices[i]], 1, __ATOMIC_RELEASE);
    }
    WinBroadcastMsg(HWND_DESKTOP, WM_SYSCOLORCHANGE, 0, 0, BMSG_SEND | BMSG_DESCENDANTS);
    WinInvalidateRect(HWND_DESKTOP, NULL, TRUE);
    return TRUE;
}

BOOL WinSetSysValue(HWND desktop, LONG index, LONG value)
{
    if (desktop != HWND_DESKTOP || !getAnchorBlockNoHAB())
        return FALSE;
    /* Settable values are SV_SWAPBUTTON to SV_ERRORDURATION, less the reserved pair, and
       SV_EXTRAKEYBEEP upward; the metrics between them describe this implementation. */
    if (index < 0 || index >= SV_CSYSVALUES ||
        (index > SV_ERRORDURATION && index < SV_EXTRAKEYBEEP) || index == SV_RESERVEDFIRST1 ||
        index == SV_RESERVEDLAST1)
    {
        WinSetErrorInfo(PMERR_PARAMETER_OUT_OF_RANGE, 0);
        return FALSE;
    }
    /* A sizing border never becomes narrower than the plain border. */
    if (index == SV_CXSIZEBORDER || index == SV_CYSIZEBORDER)
    {
        LONG minimum = WinQuerySysValue(desktop, index - SV_CXSIZEBORDER + SV_CXBORDER);
        if (value < minimum)
            value = minimum;
    }
    __atomic_store_n(&changed_values[index], value, __ATOMIC_RELAXED);
    __atomic_store_n(&value_changed[index], 1, __ATOMIC_RELEASE);
    return TRUE;
}
