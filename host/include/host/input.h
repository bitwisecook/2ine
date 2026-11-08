/* Public domain. No rights reserved. Wholly AI-authored. */
/* Host input: window events, keyboard, mouse and pointer shapes. */
#ifndef TWOINE_HOST_INPUT_H
#define TWOINE_HOST_INPUT_H
#include <stdint.h>
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
/* Pixels are top-down 0xAARRGGBB values. The factory copies their contents. */
LxHostPointer *lx_host_pointer_create_color(const uint32_t *pixels, unsigned width, unsigned height,
                                            unsigned hot_x, unsigned hot_y);
void lx_host_pointer_destroy(LxHostPointer *pointer);
/* Selecting a shape preserves visibility; NULL removes the current pointer.
   The caller applies its display-level policy through lx_host_pointer_show. */
int lx_host_pointer_set(LxHostPointer *pointer);

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
    LX_HOST_EVENT_FOCUS_LOST,
    LX_HOST_EVENT_INPUT_RESET,
    LX_HOST_EVENT_MINIMIZED,
    LX_HOST_EVENT_RESTORED,
    LX_HOST_EVENT_MOVED
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
    unsigned clicks;
    char text[64];
} LxHostEvent;

int lx_host_video_poll(LxHostEvent *event);
int lx_host_video_wait(LxHostEvent *event);
int lx_host_video_wait_timeout(LxHostEvent *event, int milliseconds);
int lx_host_video_push(const LxHostEvent *event);
/* The Unicode character the active host layout gives a physical key (IBM PC set 1), or 0. */
uint32_t lx_host_key_character(uint32_t scancode, int shifted);
void lx_host_mouse_position(int *x, int *y);
int lx_host_mouse_warp(int x, int y);
int lx_host_pointer_show(int show);
#endif
