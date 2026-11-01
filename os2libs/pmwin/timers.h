/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_PM_TIMERS_H
#define TWOINE_PM_TIMERS_H
#include "os2libs/pmwin.h"
/* Private timer callbacks run on the queue thread and must not change its timer list. */
ULONG lx_pm_timer_create(HWND owner, ULONG interval, void (*callback)(void));
BOOL lx_pm_timer_stop(ULONG id);
#endif
