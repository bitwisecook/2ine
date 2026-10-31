/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_TERMINAL_H
#define TWOINE_HOST_TERMINAL_H
#include <stdint.h>
int lx_host_terminal_open(int *width, int *height);
void lx_host_terminal_close(void);
void lx_host_terminal_refresh(void);
void lx_host_terminal_cursor(int x, int y);
/* Cells contain a PC character byte followed by its color/attribute byte. */
void lx_host_terminal_cells(int x, int y, const uint16_t *cells, int count, int cursor_x,
                            int cursor_y, int commit);
#endif
