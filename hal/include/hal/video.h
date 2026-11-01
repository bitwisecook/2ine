/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_HOST_VIDEO_H
#define TWOINE_HOST_VIDEO_H
#include <stdint.h>
typedef struct LxHostWindow LxHostWindow;
typedef struct LxHostPointer LxHostPointer;
typedef enum
{
    LX_HOST_POINTER_ARROW,
    LX_HOST_POINTER_TEXT,
    LX_HOST_POINTER_WAIT,
    LX_HOST_POINTER_SIZE,
    LX_HOST_POINTER_MOVE,
    LX_HOST_POINTER_NWSE,
    LX_HOST_POINTER_NESW,
    LX_HOST_POINTER_WE,
    LX_HOST_POINTER_NS,
    LX_HOST_POINTER_ILLEGAL
} LxHostPointerShape;
LxHostPointer *lx_host_pointer_create(LxHostPointerShape shape);
void lx_host_pointer_destroy(LxHostPointer *pointer);
int lx_host_pointer_set(LxHostPointer *pointer);
typedef struct LxHostRect
{
    int x, y, w, h;
} LxHostRect;
typedef enum LxHostEventType
{
    LX_HOST_EVENT_OTHER,
    LX_HOST_EVENT_QUIT,
    LX_HOST_EVENT_EXPOSED,
    LX_HOST_EVENT_SHOWN,
    LX_HOST_EVENT_CLOSE,
    LX_HOST_EVENT_RESIZED,
    LX_HOST_EVENT_MOTION,
    LX_HOST_EVENT_BUTTON_DOWN,
    LX_HOST_EVENT_BUTTON_UP,
    LX_HOST_EVENT_KEY_DOWN,
    LX_HOST_EVENT_KEY_UP,
    LX_HOST_EVENT_TEXT,
    LX_HOST_EVENT_FOCUS_GAINED,
    LX_HOST_EVENT_FOCUS_LOST
} LxHostEventType;
typedef enum
{
    LX_HOST_KEY_BACKSPACE = 0x100,
    LX_HOST_KEY_TAB,
    LX_HOST_KEY_RETURN,
    LX_HOST_KEY_ESCAPE,
    LX_HOST_KEY_HOME,
    LX_HOST_KEY_END,
    LX_HOST_KEY_LEFT,
    LX_HOST_KEY_RIGHT,
    LX_HOST_KEY_UP,
    LX_HOST_KEY_DOWN,
    LX_HOST_KEY_DELETE,
    LX_HOST_KEY_INSERT,
    LX_HOST_KEY_PAGEUP,
    LX_HOST_KEY_PAGEDOWN,
    LX_HOST_KEY_SHIFT,
    LX_HOST_KEY_CTRL,
    LX_HOST_KEY_ALT,
    LX_HOST_KEY_KP_ENTER,
    LX_HOST_KEY_F1 = 0x120
} LxHostKey;
enum
{
    LX_HOST_MOD_SHIFT = 1,
    LX_HOST_MOD_CTRL = 2,
    LX_HOST_MOD_ALT = 4
};
typedef struct LxHostEvent
{
    LxHostEventType type;
    uint64_t timestamp;
    void *window_data;
    int x, y, width, height, button;
    uint32_t key, modifiers, repeat;
    uint32_t scancode; /* IBM PC set 1 physical key position, without release bit. */
    char text[64];
} LxHostEvent;
int lx_host_video_initialize(int *width, int *height);
void lx_host_video_quit(void);
const char *lx_host_video_error(void);
/* Milliseconds in the same time domain as LxHostEvent.timestamp. */
uint64_t lx_host_video_ticks_ms(void);
LxHostWindow *lx_host_window_create(const char *title, int x, int y, int width, int height,
                                    void *data);
void lx_host_window_destroy(LxHostWindow *window);
void *lx_host_window_data(LxHostWindow *window);
int lx_host_window_position(LxHostWindow *window, int x, int y, int width, int height);
void lx_host_window_show(LxHostWindow *window);
void lx_host_window_hide(LxHostWindow *window);
int lx_host_window_title(LxHostWindow *window, const char *title);
int lx_host_window_border(LxHostWindow *window, int bordered);
int lx_host_window_focus(LxHostWindow *window);
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
int lx_host_video_poll(LxHostEvent *event);
int lx_host_video_wait(LxHostEvent *event);
int lx_host_video_wait_timeout(LxHostEvent *event, int milliseconds);
int lx_host_window_line(LxHostWindow *window, const LxHostRect *clip, int x1, int y1, int x2,
                        int y2, uint8_t r, uint8_t g, uint8_t b);
int lx_host_video_push(const LxHostEvent *event);
void lx_host_mouse_position(int *x, int *y);
#endif
