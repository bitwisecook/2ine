/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_DESKTOP_CYCLE_H
#define TWOINE_DESKTOP_CYCLE_H
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
typedef struct LxDesktopCycle LxDesktopCycle;
LxDesktopCycle *lx_desktop_cycle_open(const char *directory, const char *session);
int lx_desktop_cycle_publish(LxDesktopCycle *cycle, const uint32_t *windows, size_t count,
                             int blocked);
int lx_desktop_cycle_focus(LxDesktopCycle *cycle, uint32_t window);
/* 1: queued/consumed, 2: modal/consumed, 0: native behavior, -1: unavailable. */
int lx_desktop_cycle_request(LxDesktopCycle *cycle, uint32_t current, int reverse,
                             uint32_t *target_pid);
uint32_t lx_desktop_cycle_receive(LxDesktopCycle *cycle);
int lx_desktop_cycle_keyup(LxDesktopCycle *cycle);
void lx_desktop_cycle_close(LxDesktopCycle *cycle);
#endif
