/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_CORE_DESKTOP_SESSION_HOOKS_H
#define TWOINE_HOST_CORE_DESKTOP_SESSION_HOOKS_H
void lx_desktop_prepare(void);
int lx_desktop_initialized(void);
void lx_desktop_visible(void *window, int visible);
void lx_desktop_forget(void *window);
void lx_desktop_poll(void);
int lx_desktop_grouped(void);
int lx_desktop_native_main_thread(void);
#endif
