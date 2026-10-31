/* Public domain. No rights reserved. Wholly AI-authored. */
/* Terminal rendering runs under the PTY harness, without a physical display. */
#include "hal/terminal.h"
#include <stdio.h>
int main(void)
{
    int width, height;
    if (!lx_host_terminal_open(&width, &height) || width < 20 || height < 2)
        return 1;
    const uint16_t cells[] = {0x0748, 0x0741, 0x074c};
    lx_host_terminal_cells(0, 0, cells, 3, 3, 0, 1);
    lx_host_terminal_cursor(0, 1);
    lx_host_terminal_refresh();
    lx_host_terminal_close();
    puts("terminal closed");
    return 0;
}
