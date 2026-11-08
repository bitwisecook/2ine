/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_PM_VIDEO_FRAMES_H
#define TWOINE_PM_VIDEO_FRAMES_H
#include "os2libs/pmwin.h"
#include "host/video_frame.h"
void *lx_pm_video_target(HWND window);
/* Submission always consumes owner, including rejected or superseded frames. */
BOOL lx_pm_video_submit(void *target, const LxHostVideoFrame *frame, const RECTL *destination,
                        void *owner, void (*release)(void *));
void lx_pm_video_release(void *target);
BOOL lx_pm_video_ready(void *target);
void lx_pm_video_pump(void *anchor);
void lx_pm_video_pump_current(void);
void lx_pm_video_forget(void *window);
void lx_pm_video_cleanup(void *anchor);
#endif
