/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "hal/terminal.h"
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>

#include <curses.h>

#include <locale.h>

enum
{
    TEXT_BACK_BLACK = 0x00,
    TEXT_BACK_BLUE = 0x10,
    TEXT_BACK_GREEN = 0x20,
    TEXT_BACK_CYAN = 0x30,
    TEXT_BACK_RED = 0x40,
    TEXT_BACK_MAGENTA = 0x50,
    TEXT_BACK_YELLOW = 0x60,
    TEXT_BACK_BROWN = 0x60,
    TEXT_BACK_WHITE = 0x70,

    TEXT_FORE_BLACK = 0x00,
    TEXT_FORE_BLUE = 0x01,
    TEXT_FORE_GREEN = 0x02,
    TEXT_FORE_CYAN = 0x03,
    TEXT_FORE_RED = 0x04,
    TEXT_FORE_MAGENTA = 0x05,
    TEXT_FORE_YELLOW = 0x06,
    TEXT_FORE_BROWN = 0x06,
    TEXT_FORE_WHITE = 0x07,

    TEXT_INTENSITY = 0x08,
    TEXT_BLINK = 0x80
};

int lx_host_terminal_open(int *width, int *height)
{
    setlocale(LC_CTYPE, "");
    if (initscr() == NULL)
    {
        fprintf(stderr, "ncurses: initscr() failed\n");
        return 0;
    }

    cbreak();
    keypad(stdscr, TRUE);
    noecho();
    start_color();
    use_default_colors();

    static const short curses_colormap[] = {COLOR_BLACK, COLOR_BLUE,    COLOR_GREEN,  COLOR_CYAN,
                                            COLOR_RED,   COLOR_MAGENTA, COLOR_YELLOW, COLOR_WHITE};

    if (COLORS >= 64)
    { // foreground and background
        for (uint32_t i = 0; i < 8; i++)
        {
            for (uint32_t j = 0; j < 8; j++)
            {
                const int pair = (int)(i | (j << 3));
                init_pair(pair, curses_colormap[i], (j == 0) ? -1 : curses_colormap[j]);
            }
        }
    }
    else if (COLORS >= 8)
    { // just foregrounds
        for (int i = 0; i < 8; i++)
            init_pair(i, curses_colormap[i], COLOR_BLACK);
    }

    // (otherwise, we won't set colors at all.)

    int scrh, scrw;
    getmaxyx(stdscr, scrh, scrw);

    *width = scrw;
    *height = scrh;
    return 1;
}
void lx_host_terminal_close(void)
{
    if (!stdscr)
        return;
    reset_shell_mode();
    printf("\n");
    fflush(stdout);
    delwin(stdscr);
    stdscr = NULL;
}
void lx_host_terminal_refresh(void)
{
    refresh();
}
void lx_host_terminal_cursor(int x, int y)
{
    move(y, x);
    refresh();
}
void lx_host_terminal_cells(int x, int y, const uint16_t *cells, int count, int cursor_x,
                            int cursor_y, int commit)
{
    const uint8_t *src = (const uint8_t *)cells;
    move(y, x);

    for (int i = 0; i < count; i++)
    {
        const uint8_t viochar = *(src++);
        const uint8_t vioattr = *(src++);

        chtype ch;
        switch (viochar)
        {
            case 0xC4:
                ch = ACS_HLINE;
                break;
            case 0xB3:
                ch = ACS_VLINE;
                break;
            case 0xDA:
                ch = ACS_ULCORNER;
                break;
            case 0xBF:
                ch = ACS_URCORNER;
                break;
            case 0xC0:
                ch = ACS_LLCORNER;
                break;
            case 0xD9:
                ch = ACS_LRCORNER;
                break;
            default:
                ch = (chtype)viochar;
                break;
        }

        chtype attr = 0;

        if (COLORS >= 64)
        { // foreground and background
            const int color = ((vioattr & 0x70) >> 1) | (vioattr & 0x7);
            attr |= COLOR_PAIR(color);
        }
        else if (COLORS >= 8)
        { // just foregrounds
            const int color = (vioattr & 0x7);
            attr |= COLOR_PAIR(color);
        }

        if (vioattr & TEXT_INTENSITY)
            attr |= A_BOLD;
        if (vioattr & TEXT_BLINK)
            attr |= A_BLINK;

        addch(ch | attr);
    }

    move(cursor_y, cursor_x);

    if (commit)
        refresh();
}
