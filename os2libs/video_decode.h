/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_VIDEO_DECODE_H
#define TWOINE_VIDEO_DECODE_H
#include "host/video_frame.h"
#include <stdint.h>
typedef struct LxVideoDecoder LxVideoDecoder;

typedef struct LxDecodedVideoFrame
{
    LxHostVideoFrame image;
    int64_t milliseconds, duration_ms;
    void *owner;
} LxDecodedVideoFrame;

typedef struct LxVideoInfo
{
    unsigned width, height;
    int64_t duration_ms;
    unsigned rate_numerator, rate_denominator;
    int audio_present;
} LxVideoInfo;

typedef int (*LxVideoAudioSink)(void *, const float *, unsigned, int64_t);
void lx_video_decoder_audio_sink(LxVideoDecoder *decoder, LxVideoAudioSink sink, void *context);
LxVideoDecoder *lx_video_decoder_open(const char *host_path, LxVideoInfo *info);
LxVideoDecoder *lx_video_decoder_open_audio(const char *host_path, LxVideoInfo *info, int audio);
int lx_video_decoder_next(LxVideoDecoder *decoder, LxDecodedVideoFrame *frame);
int lx_video_decoder_seek(LxVideoDecoder *decoder, int64_t milliseconds);
void lx_video_frame_release(LxDecodedVideoFrame *frame);
void lx_video_decoder_close(LxVideoDecoder *decoder);
#endif
