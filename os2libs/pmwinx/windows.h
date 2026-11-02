/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_OPEN32_WINDOWS_H
#define TWOINE_OPEN32_WINDOWS_H
#include "os2libs/pmwinx/classes.h"
#include "os2libs/pmwin.h"
typedef struct Open32Scroll
{
    LONG minimum, maximum, position;
    BOOL visible;
} Open32Scroll;
typedef struct Open32Window
{
    HWND handle, client;
    Open32Class *class;
    ULONG style, extended_style, parameter, instance, menu, parent, procedure, user_data;
    ULONG mdi_menu, mdi_next_id, mdi_active, system_menu;
    uint8 *extra;
    LONG x, y, width, height;
    char *title;
    LxGuestMapping *creation;
    struct Open32Property *properties;
    Open32Scroll scroll[3];
    BOOL scroll_dragging;
    LONG scroll_drag_offset, scroll_track_position;
    BOOL erase_pending;
    int destroyed;
    struct Open32Window *next, *creating_previous;
} Open32Window;
Open32Window *open32FindWindow(ULONG handle);
ULONG open32ClientWindow(ULONG handle);
ULONG open32PublicWindow(ULONG handle);
ULONG open32WindowCallback(Open32Window *window, ULONG message, ULONG first, ULONG second);
void open32PropertiesCleanup(Open32Window *window);
#endif
