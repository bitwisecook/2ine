/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_WINDOW_H
#define TWOINE_HOST_WINDOW_H
#include <stdint.h>
/* Host top-level windows, their render targets and the display they share. */
typedef struct LxHostWindow LxHostWindow;

typedef struct LxHostRect
{
    int x, y, w, h;
} LxHostRect;

/* Each successful initialization owns one reference, released by video_quit. */
int lx_host_video_initialize(int *width, int *height);
/* Release a renderer's GL binding before a separate graphics context is bound. */
int lx_host_video_release_graphics(void);
/* Install/remove only while graphics users are quiescent. The callback releases
   an external graphics API's current binding before renderer operations. */
void lx_host_video_set_graphics_release(void (*release)(void));
void lx_host_video_quit(void);
const char *lx_host_video_error(void);
/* Milliseconds in the same time domain as LxHostEvent.timestamp (host/input.h). */
uint64_t lx_host_video_ticks_ms(void);
LxHostWindow *lx_host_window_create(const char *title, int x, int y, int width, int height,
                                    void *data);
void lx_host_window_destroy(LxHostWindow *window);
void *lx_host_window_data(LxHostWindow *window);
int lx_host_window_position(LxHostWindow *window, int x, int y, int width, int height);
/* Moves a client-area y below the display's reserved top area and the window's title bar. */
void lx_host_window_constrain(LxHostWindow *window, int x, int *y, int width, int height);
/* Host fullscreen preserves guest coordinates and the render target dimensions. */
int lx_host_window_fullscreen(LxHostWindow *window, int enabled);
int lx_host_window_is_fullscreen(LxHostWindow *window);
void lx_host_window_show(LxHostWindow *window);
void lx_host_window_hide(LxHostWindow *window);
int lx_host_window_minimize(LxHostWindow *window, int minimized);
int lx_host_window_title(LxHostWindow *window, const char *title);
int lx_host_window_border(LxHostWindow *window, int bordered);
int lx_host_window_resizable(LxHostWindow *window, int resizable);
int lx_host_window_is_resizable(LxHostWindow *window);
int lx_host_window_focus(LxHostWindow *window);
/* Opt-in mouse capture starts released and is never restored by focus alone. */
int lx_host_window_set_capture(LxHostWindow *window, int captured);
int lx_host_window_is_captured(LxHostWindow *window);
int lx_host_window_mask(LxHostWindow *window, const LxHostRect *clip, int x, int y, int width,
                        int height, const uint8_t *coverage, uint8_t red, uint8_t green,
                        uint8_t blue);
int lx_host_window_present(LxHostWindow *window);
int lx_host_window_fill(LxHostWindow *window, const LxHostRect *clip, const LxHostRect *rect,
                        uint8_t red, uint8_t green, uint8_t blue);
int lx_host_window_read(LxHostWindow *window, const LxHostRect *rect, uint32_t *rgb);
int lx_host_window_image(LxHostWindow *window, const LxHostRect *clip, const LxHostRect *rect,
                         const uint32_t *rgb);
int lx_host_window_pixel(LxHostWindow *window, int x, int y, uint8_t *red, uint8_t *green,
                         uint8_t *blue);
int lx_host_window_line(LxHostWindow *window, const LxHostRect *clip, int x1, int y1, int x2,
                        int y2, uint8_t r, uint8_t g, uint8_t b);
#endif
