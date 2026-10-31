/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_VIDEO_H
#define TWOINE_HOST_VIDEO_H
#include <stdint.h>
typedef struct LxHostWindow LxHostWindow;
typedef struct LxHostRect
{
    int x, y, w, h;
} LxHostRect;
typedef enum LxHostEventType
{
    LX_HOST_EVENT_OTHER,
    LX_HOST_EVENT_QUIT,
    LX_HOST_EVENT_EXPOSED,
    LX_HOST_EVENT_SHOWN
} LxHostEventType;
typedef struct LxHostEvent
{
    LxHostEventType type;
    uint64_t timestamp;
    void *window_data;
} LxHostEvent;
int lx_host_video_initialize(int *width, int *height);
void lx_host_video_quit(void);
const char *lx_host_video_error(void);
LxHostWindow *lx_host_window_create(const char *title, int x, int y, int width, int height,
                                    void *data);
void lx_host_window_destroy(LxHostWindow *window);
void *lx_host_window_data(LxHostWindow *window);
void lx_host_window_position(LxHostWindow *window, int x, int y, int width, int height);
void lx_host_window_show(LxHostWindow *window);
int lx_host_window_present(LxHostWindow *window);
int lx_host_window_fill(LxHostWindow *window, const LxHostRect *clip, const LxHostRect *rect,
                        uint8_t red, uint8_t green, uint8_t blue);
int lx_host_window_pixel(LxHostWindow *window, int x, int y, uint8_t *red, uint8_t *green,
                         uint8_t *blue);
int lx_host_video_poll(LxHostEvent *event);
int lx_host_video_wait(LxHostEvent *event);
int lx_host_video_push(const LxHostEvent *event);
void lx_host_mouse_position(int *x, int *y);
#endif
