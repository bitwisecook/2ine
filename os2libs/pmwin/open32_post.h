/* Public domain. No rights reserved. Wholly AI-authored. */
/* Tagged Windows packets retain their message number through the PM owner queue. */
#ifndef TWOINE_PM_OPEN32_POST_H
#define TWOINE_PM_OPEN32_POST_H
#include "os2libs/pmwin.h"
#define LX_PM_OPEN32_POST 0x7ffef001u
BOOL lx_pm_mark_open32_window(HWND window);
BOOL lx_pm_post_open32_message(HWND window, ULONG message, ULONG first, ULONG second);
#endif
