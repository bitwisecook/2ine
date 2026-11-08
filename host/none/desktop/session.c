/* Public domain. No rights reserved. Wholly AI-authored. */
#include "host/core/desktop/session_hooks.h"

void lx_desktop_prepare(void) {}

int lx_desktop_initialized(void)
{
    return 1;
}

void lx_desktop_visible(void *window, int visible)
{
    (void)window;
    (void)visible;
}

void lx_desktop_forget(void *window)
{
    (void)window;
}

void lx_desktop_poll(void) {}

int lx_desktop_grouped(void)
{
    return 0;
}

int lx_desktop_native_main_thread(void)
{
    return 1;
}
