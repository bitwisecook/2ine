/* Public domain. No rights reserved. Wholly AI-authored. */
/* Cross-process window-cycle order, independent of how windows are registered. */
#ifndef TWOINE_HOST_CORE_DESKTOP_CYCLE_ORDER_H
#define TWOINE_HOST_CORE_DESKTOP_CYCLE_ORDER_H
#include <stddef.h>
#include <stdint.h>

typedef struct LxDesktopCycleWindow
{
    uint64_t member, order;
    uint32_t window;
} LxDesktopCycleWindow;

/* Stable creation order, independent of focus/Z order; absent origins wrap. */
int lx_desktop_cycle_next(const LxDesktopCycleWindow *windows, size_t count, uint64_t member,
                          uint32_t window, int reverse, LxDesktopCycleWindow *output);
#endif
