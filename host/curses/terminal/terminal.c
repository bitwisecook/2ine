/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "host/terminal.h"
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <termios.h>
#include <poll.h>
#include <errno.h>

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
    TEXT_BLINK = 0x80,
    TEXT_BACK_MASK = 0x70,
    TEXT_FORE_MASK = 0x07
};

/* The VIO screen captured when output is redirected. */
enum
{
    LOG_ROWS = 25,
    LOG_COLUMNS = 80
};

/* Code page 437 box-drawing characters drawn with curses line graphics. */
enum
{
    CP437_HORIZONTAL = 0xC4,
    CP437_VERTICAL = 0xB3,
    CP437_DOWN_RIGHT = 0xDA,
    CP437_DOWN_LEFT = 0xBF,
    CP437_UP_RIGHT = 0xC0,
    CP437_UP_LEFT = 0xD9,
    ASCII_DELETE = 0x7f
};

static int redirected;
static unsigned char log_cells[LOG_ROWS][LOG_COLUMNS];
static unsigned char log_dirty[LOG_ROWS];

int lx_host_terminal_read_input(int wait)
{
    struct pollfd input = {STDIN_FILENO, POLLIN, 0};
    int ready;
    do
        ready = poll(&input, 1, wait ? -1 : 0);
    while (ready < 0 && errno == EINTR);
    if (ready <= 0 || !(input.revents & (POLLIN | POLLHUP)))
        return -1;
    unsigned char byte;
    ssize_t count;
    do
        count = read(STDIN_FILENO, &byte, 1);
    while (count < 0 && errno == EINTR);
    return count == 1 ? byte : -1;
}

void lx_host_terminal_flush_input(void)
{
    if (isatty(STDIN_FILENO))
        tcflush(STDIN_FILENO, TCIFLUSH);
    if (stdscr)
        flushinp();
}

int lx_host_terminal_open(int *width, int *height)
{
    redirected = !isatty(STDOUT_FILENO);
    if (redirected)
    {
        memset(log_cells, ' ', sizeof log_cells);
        memset(log_dirty, 0, sizeof log_dirty);
        *width = 80;
        *height = 25;
        return 1;
    }
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
    if (redirected)
        return;
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
    if (redirected)
    {
        for (int y = 0; y < LOG_ROWS; y++)
        {
            if (!log_dirty[y])
                continue;
            log_dirty[y] = 0;
            int length = LOG_COLUMNS;
            while (length && log_cells[y][length - 1] == ' ')
                length--;
            if (length)
            {
                fwrite(log_cells[y], 1, (size_t)length, stdout);
                fputc('\n', stdout);
            }
        }
        fflush(stdout);
        return;
    }
    refresh();
}

void lx_host_terminal_cursor(int x, int y)
{
    if (redirected)
        return;
    move(y, x);
    refresh();
}

void lx_host_terminal_cells(int x, int y, const uint16_t *cells, int count, int cursor_x,
                            int cursor_y, int commit)
{
    if (redirected)
    {
        for (int i = 0; i < count; i++)
        {
            int row = y + (x + i) / LOG_COLUMNS, column = (x + i) % LOG_COLUMNS;
            if (row < 0 || row >= LOG_ROWS || column < 0)
                continue;
            unsigned char ch = (unsigned char)(cells[i] & 0xff);
            if (ch < ' ' || ch == ASCII_DELETE)
                ch = ' ';
            if (log_cells[row][column] != ch)
            {
                log_cells[row][column] = ch;
                log_dirty[row] = 1;
            }
        }
        if (commit)
            lx_host_terminal_refresh();
        return;
    }
    const uint8_t *src = (const uint8_t *)cells;
    move(y, x);

    for (int i = 0; i < count; i++)
    {
        const uint8_t viochar = *(src++);
        const uint8_t vioattr = *(src++);

        chtype ch;
        switch (viochar)
        {
            case CP437_HORIZONTAL:
                ch = ACS_HLINE;
                break;
            case CP437_VERTICAL:
                ch = ACS_VLINE;
                break;
            case CP437_DOWN_RIGHT:
                ch = ACS_ULCORNER;
                break;
            case CP437_DOWN_LEFT:
                ch = ACS_URCORNER;
                break;
            case CP437_UP_RIGHT:
                ch = ACS_LLCORNER;
                break;
            case CP437_UP_LEFT:
                ch = ACS_LRCORNER;
                break;
            default:
                ch = (chtype)viochar;
                break;
        }

        chtype attr = 0;

        if (COLORS >= 64)
        { // foreground and background
            const int color = ((vioattr & TEXT_BACK_MASK) >> 1) | (vioattr & TEXT_FORE_MASK);
            attr |= COLOR_PAIR(color);
        }
        else if (COLORS >= 8)
        { // just foregrounds
            const int color = (vioattr & TEXT_FORE_MASK);
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
