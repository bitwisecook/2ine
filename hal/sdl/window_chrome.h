/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_WINDOW_CHROME_H
#define TWOINE_WINDOW_CHROME_H
typedef struct LxWindowChrome LxWindowChrome;
LxWindowChrome *lx_window_chrome_create(void *window, void (*toggle)(void *), void *userdata);
void lx_window_chrome_capture(LxWindowChrome *chrome, int captured);
void lx_window_chrome_destroy(LxWindowChrome *chrome);
#endif
