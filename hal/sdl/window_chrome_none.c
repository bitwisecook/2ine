/* Public domain. No rights reserved. Wholly AI-authored. */
#include "window_chrome.h"
#include <stddef.h>
LxWindowChrome *lx_window_chrome_create(void *window, void (*toggle)(void *), void *userdata)
{
    (void)window;
    (void)toggle;
    (void)userdata;
    return NULL;
}
void lx_window_chrome_capture(LxWindowChrome *chrome, int captured)
{
    (void)chrome;
    (void)captured;
}
void lx_window_chrome_destroy(LxWindowChrome *chrome)
{
    (void)chrome;
}
