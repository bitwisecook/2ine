/* Public domain. No rights reserved. Wholly AI-authored. */
/* Tagged Windows packets retain their message number through the PM owner queue. */
#ifndef TWOINE_PM_OPEN32_POST_H
#define TWOINE_PM_OPEN32_POST_H
#include "os2libs/pmwin.h"
#define LX_PM_OPEN32_POST 0x7ffef001u
/* A mailbox record for no particular window: end this worker's PM session. */
#define LX_PM_SESSION_END 0x7ffef002u
/* Tells every other live PM worker in the ball to end its session. FALSE when
   one could not be told. Without a ball this worker is the whole session. */
BOOL lx_pm_end_session(void);
BOOL lx_pm_mark_open32_window(HWND window);
BOOL lx_pm_post_open32_message(HWND window, ULONG message, ULONG first, ULONG second);
typedef BOOL (*LxPmPaintFilter)(HWND window, void *context);
BOOL lx_pm_peek_open32_message(HAB hab, PQMSG message, LxPmPaintFilter paint_filter,
                               void *context);
BOOL lx_pm_get_open32_message(HAB hab, PQMSG message, LxPmPaintFilter paint_filter,
                              void *context);
#endif
