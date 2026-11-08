/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_VIDEO_FRAME_H
#define TWOINE_VIDEO_FRAME_H
#include "hal/video.h"
#include <stddef.h>

typedef enum LxHostFrameFormat
{
    LX_HOST_FRAME_BGRA,
    LX_HOST_FRAME_YUV420P,
    LX_HOST_FRAME_NV12
} LxHostFrameFormat;

typedef enum LxHostFrameColors
{
    LX_HOST_FRAME_BT601_LIMITED,
    LX_HOST_FRAME_BT601_FULL,
    LX_HOST_FRAME_BT709_LIMITED,
    LX_HOST_FRAME_BT709_FULL
} LxHostFrameColors;

/* Read-only top-down planes remain owned by the caller until upload returns. */
typedef struct LxHostVideoFrame
{
    unsigned width, height;
    LxHostFrameFormat format;
    LxHostFrameColors colors;
    const uint8_t *planes[3];
    size_t strides[3], lengths[3];
} LxHostVideoFrame;

/* One upload, any number of visible rectangles. Renderer access is owner-thread-only. */
int lx_host_window_video_frame(LxHostWindow *window, const LxHostVideoFrame *frame,
                               const LxHostRect *destination, const LxHostRect *clips,
                               size_t count);
#endif
