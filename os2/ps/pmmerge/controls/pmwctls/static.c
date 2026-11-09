/* Public domain. No rights reserved. Wholly AI-authored. */
/* WC_STATIC: frame and fill shapes, and the group box. */
#include "os2/include/private/pmwctlp.h"
#include "os2/include/private/pmgrep.h"
#include "os2/ps/common/pm_text.h"

enum
{
    /* The low style bits below SS_AUTOSIZE select the static type. */
    STATIC_TYPE_MASK = SS_AUTOSIZE - 1
};

/* Group boxes and frames leave the controls inside them visible. */
BOOL staticIsFrame(const Window *window)
{
    ULONG type = window->style & STATIC_TYPE_MASK;
    return type == SS_GROUPBOX || (type >= SS_FGNDFRAME && type <= SS_BKGNDFRAME);
}

/* SS_FGNDRECT through SS_BKGNDFRAME: foreground, halftone or background, as a filled
   rectangle or a one-pixel outline. Halftone is the midpoint of the other two. */
void paintStaticShape(Window *window, HPS space)
{
    ULONG type = window->style & STATIC_TYPE_MASK;
    ULONG foreground = (ULONG)windowPresentationColor(window, TRUE, 0);
    ULONG background =
        (ULONG)windowPresentationColor(window, FALSE, WinQuerySysColor(HWND_DESKTOP, -34, 0));
    ULONG tone = type == SS_FGNDRECT || type == SS_FGNDFRAME           ? 0
                 : type == SS_HALFTONERECT || type == SS_HALFTONEFRAME ? 1
                                                                       : 2;
    LONG color =
        (LONG)(tone == 0   ? foreground
               : tone == 2 ? background
                           : ((foreground >> 1) & 0x7f7f7f) + ((background >> 1) & 0x7f7f7f));
    if (!lx_pm_color_table(space, 0, 2, 254, 1, &color))
        return;
    if (type <= SS_BKGNDRECT)
    {
        RECTL area = {0, 0, window->w, window->h};
        WinFillRect(space, &area, 254);
        return;
    }
    RECTL edges[] = {{0, 0, window->w, 1},
                     {0, window->h - 1, window->w, window->h},
                     {0, 0, 1, window->h},
                     {window->w - 1, 0, window->w, window->h}};
    for (size_t i = 0; i < sizeof edges / sizeof *edges; i++)
        WinFillRect(space, &edges[i], 254);
}

/* SS_GROUPBOX: an etched frame whose top edge runs through the middle of the caption line,
   broken where the caption is drawn. */
void paintGroupBox(Window *window, HPS space, const LxHostFontMetrics *metrics)
{
    LONG w = window->w, top = window->h - (metrics->ascent + metrics->descent) / 2;
    if (w < 3 || top < 3)
        return;
    LONG gap_left = w, gap_right = w;
    LxHostFont *font = lx_pm_font(space);
    if (font && window->text && *window->text)
    {
        LONG width = 0;
        for (const uint8 *p = (const uint8 *)window->text; *p; p++)
        {
            if (*p == '~' && p[1])
                p++;
            int advance = lx_host_font_advance(font, lx_pm_codepoint(*p));
            if (advance > 0)
                width += advance;
        }
        gap_left = 1;
        gap_right = 3 + width + 2 < w ? 3 + width + 2 : w;
    }

    const struct
    {
        RECTL rect;
        LONG color;
    } edges[] = {
        {{0, 1, 1, top}, CLR_DARKGRAY},
        {{0, 1, w - 1, 2}, CLR_DARKGRAY},
        {{w - 2, 1, w - 1, top}, CLR_DARKGRAY},
        {{0, top - 1, gap_left, top}, CLR_DARKGRAY},
        {{gap_right, top - 1, w - 1, top}, CLR_DARKGRAY},
        {{1, 2, 2, top - 1}, CLR_WHITE},
        {{1, 0, w, 1}, CLR_WHITE},
        {{w - 1, 0, w, top}, CLR_WHITE},
        {{1, top - 2, gap_left, top - 1}, CLR_WHITE},
        {{gap_right, top - 2, w - 2, top - 1}, CLR_WHITE},
    };

    for (size_t i = 0; i < sizeof edges / sizeof *edges; i++)
        if (edges[i].rect.xLeft < edges[i].rect.xRight)
            WinFillRect(space, (PRECTL)&edges[i].rect, edges[i].color);
}
