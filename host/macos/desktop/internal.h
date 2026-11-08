/* Public domain. No rights reserved. Wholly AI-authored. */
/* Private interface between the macOS desktop session, its window cycling,
   its application icon and the per-program application bundles. The session
   parts run on the SDL main thread. */
#ifndef TWOINE_HOST_MACOS_DESKTOP_INTERNAL_H
#define TWOINE_HOST_MACOS_DESKTOP_INTERNAL_H
#import <AppKit/AppKit.h>
#include "formats/resources/icon.h"
#include <stddef.h>

/* Opens the session's cycle registry and installs the Cmd-` monitor. On
   failure nothing stays installed and *error describes the step that failed. */
int lx_macos_window_cycle_open(const char **error);
/* Publishes the visible, eligible windows and the focused one; 0 on failure. */
int lx_macos_window_cycle_refresh(void);
/* Activates a window another process selected for this one. */
void lx_macos_window_cycle_receive(void);
/* An application icon from a 2INERGBA image or any ImageIO format, or nil. */
NSImage *lx_macos_app_icon_read(const char *path);

/* Writes an OS/2 icon as a PNG bundle icon. */
int lx_macos_bundle_icon_write(const char *path, const TbIconImage *image);
/* The embedded 2ine ball PNG for programs without an icon; caller frees. */
unsigned char *lx_macos_bundle_ball_icon_png(size_t *size);

static inline int lx_macos_hex_value(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}
#endif
