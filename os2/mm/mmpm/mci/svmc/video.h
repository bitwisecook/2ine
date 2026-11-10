/* Public domain. No rights reserved. Wholly AI-authored. */
/* MMPM's digital-video device. Every device field changes only under
   lx_dgv_monitor. Decoder workers hand retained frames to the PM owner (PMMERGE's
   video-frame service); they never render. */
#ifndef TWOINE_MMPM_VIDEO_H
#define TWOINE_MMPM_VIDEO_H
#include "os2/include/private/export.h"
#include "os2/include/private/os2mep.h"
#include "os2/include/private/pmp.h"
#include "os2/mm/mmpm/mci/svmc/digital_video.h"
#include "os2/mm/video/decode/video_decode.h"
#include "host/sync.h"

enum
{
    VIDEO_DEVICES = 16,
    /* Video device IDs start above the waveaudio and sequencer IDs. */
    VIDEO_FIRST_ID = 0x8000,
    /* Ring of decoded sound frames between the decoder and the host mixer: two seconds. */
    VIDEO_AUDIO_FRAMES = 2 * LX_VIDEO_AUDIO_RATE,
    /* Bytes of an alias, including its terminator. */
    VIDEO_ALIAS_SIZE = 128,
    /* Bytes of the longest element (file) name copied from the guest. */
    VIDEO_NAME_LIMIT = 4096,
    /* How long a paused or waiting worker sleeps before it re-checks the device. */
    VIDEO_POLL_MS = 20
};

typedef struct LxDgvAudio
{
    float *samples;
    uint64_t *tags;
    uint64_t rendered, end, limit;
    int64_t origin;
    int paused;
    float volume;
} LxDgvAudio;

typedef struct LxDgvDevice
{
    USHORT id;
    LxVideoDecoder *decoder;
    LxVideoInfo info;
    void *target;
    LxHostThread *thread;
    int audio;
    LxDgvAudio sound;
    HWND window, frame, notify_window;
    RECTL destination;
    uint32_t mode, format, notify, user, error, canceled;
    float volume;
    char alias[VIDEO_ALIAS_SIZE];
    int stop, closing;
    int64_t position, until;
    uint64_t started, paused_at;
} LxDgvDevice;

extern LxDgvDevice lx_dgv_devices[VIDEO_DEVICES];
extern LxHostMonitor *lx_dgv_monitor;

/* video_device.c */
/* Creates the monitor once; returns 0 if it cannot. */
int lx_dgv_initialize(void);
void lx_dgv_reset(LxDgvDevice *device);
LxDgvDevice *lx_dgv_find(USHORT id);
uint32_t lx_dgv_position_units(LxDgvDevice *device, int64_t milliseconds);
/* Converts a position in the device's time format; -1 if out of range. */
int64_t lx_dgv_milliseconds(LxDgvDevice *device, ULONG value);
/* Whether a file name has a video extension. */
int lx_dgv_video_name(const char *name);
int lx_dgv_set_window(LxDgvDevice *device, HWND window);
int lx_dgv_create_window(LxDgvDevice *device);
ULONG lx_dgv_load_element(ULONG address, LxVideoDecoder **decoder, LxVideoInfo *info);
ULONG lx_dgv_open(ULONG flags, unsigned char *parameters);

/* video_audio.c */
int lx_dgv_start_audio(LxDgvDevice *device, int64_t from, int64_t to);
int64_t lx_dgv_media_clock(LxDgvDevice *device);
void lx_dgv_audio_terminate(void);

/* video_play.c */
void *lx_dgv_play_worker(void *device);
/* Stops playback; reason is the MM_MCINOTIFY code of the interrupted play. */
void lx_dgv_stop_reason(LxDgvDevice *device, unsigned reason);
void lx_dgv_stop(LxDgvDevice *device);
#endif
