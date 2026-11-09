/* Public domain. No rights reserved. Wholly AI-authored. */
/* Digital-video sound: the decoder fills a ring that the host mixer drains;
   the ring position is the media clock. */
#include "os2/mm/mmpm/mci/svmc/video.h"
#include "emulator/audio_mixer.h"
#include "host/clock.h"

/* The decoder waits this long for the mixer to drain a full ring. */
enum
{
    RING_FULL_WAIT_MS = 2
};

static int audio_registered;

static int fill_audio(void *context, float *samples, int count, int frequency)
{
    (void)context;
    /* The ring holds decoder-rate frames; a mixer at another rate gets none. */
    if (frequency != LX_VIDEO_AUDIO_RATE)
        return 1;
    lx_host_monitor_lock(lx_dgv_monitor);
    for (unsigned device = 0; device < VIDEO_DEVICES; device++)
    {
        LxDgvDevice *d = &lx_dgv_devices[device];
        LxDgvAudio *sound = &d->sound;
        if (!d->audio || sound->paused)
            continue;
        for (int i = 0; i < count; i++)
        {
            if (sound->rendered >= sound->limit)
                continue;
            uint64_t at = sound->rendered++, slot = at % VIDEO_AUDIO_FRAMES;
            if (sound->tags[slot] == at)
            {
                samples[LX_VIDEO_AUDIO_CHANNELS * i] +=
                    sound->samples[slot * LX_VIDEO_AUDIO_CHANNELS] * sound->volume;
                samples[LX_VIDEO_AUDIO_CHANNELS * i + 1] +=
                    sound->samples[slot * LX_VIDEO_AUDIO_CHANNELS + 1] * sound->volume;
                sound->tags[slot] = UINT64_MAX;
            }
        }
    }
    lx_host_monitor_unlock(lx_dgv_monitor);
    return 1;
}

static int enqueue_audio(void *context, const float *samples, unsigned count, int64_t position)
{
    LxDgvDevice *d = context;
    unsigned done = 0;
    int64_t start = position - d->sound.origin;
    if (d->sound.limit != UINT64_MAX)
    {
        if (start >= (int64_t)d->sound.limit)
            return 1;
        if ((int64_t)count > (int64_t)d->sound.limit - start)
            count = (unsigned)((int64_t)d->sound.limit - start);
    }
    while (done < count)
    {
        lx_host_monitor_lock(lx_dgv_monitor);
        int stopped = d->stop;
        if (stopped || !d->audio || !lx_pm_video_ready(d->target))
        {
            lx_host_monitor_unlock(lx_dgv_monitor);
            return 0;
        }
        uint64_t rendered = d->sound.rendered;
        int64_t at = start + done;
        if (at < 0 || (uint64_t)at < rendered)
        {
            uint64_t discard = (uint64_t)((int64_t)rendered - at);
            if (discard > count - done)
                discard = count - done;
            done += (unsigned)discard;
            lx_host_monitor_unlock(lx_dgv_monitor);
            continue;
        }
        if ((uint64_t)at >= rendered + VIDEO_AUDIO_FRAMES)
        {
            lx_host_monitor_unlock(lx_dgv_monitor);
            lx_host_sleep_ms(RING_FULL_WAIT_MS);
            continue;
        }
        uint64_t available = rendered + VIDEO_AUDIO_FRAMES - (uint64_t)at;
        unsigned amount = count - done;
        if (amount > available)
            amount = (unsigned)available;
        for (unsigned i = 0; i < amount; i++)
        {
            uint64_t index = (uint64_t)at + i, slot = index % VIDEO_AUDIO_FRAMES;
            d->sound.tags[slot] = index;
            d->sound.samples[slot * LX_VIDEO_AUDIO_CHANNELS] =
                samples[(done + i) * LX_VIDEO_AUDIO_CHANNELS];
            d->sound.samples[slot * LX_VIDEO_AUDIO_CHANNELS + 1] =
                samples[(done + i) * LX_VIDEO_AUDIO_CHANNELS + 1];
        }
        if ((uint64_t)at + amount > d->sound.end)
            d->sound.end = (uint64_t)at + amount;
        lx_host_monitor_unlock(lx_dgv_monitor);
        done += amount;
    }
    return 1;
}

int64_t lx_dgv_media_clock(LxDgvDevice *d)
{
    if (d->audio)
    {
        return d->sound.origin / LX_VIDEO_AUDIO_FRAMES_PER_MS +
               (int64_t)(d->sound.rendered / LX_VIDEO_AUDIO_FRAMES_PER_MS);
    }
    return (int64_t)lx_host_video_ticks_ms() - (int64_t)d->started;
}

int lx_dgv_start_audio(LxDgvDevice *d, int64_t from, int64_t to)
{
    if (!d->info.audio_present)
        return 1;
    d->sound = (LxDgvAudio){.paused = 1,
                            .origin = from * LX_VIDEO_AUDIO_FRAMES_PER_MS,
                            .volume = d->volume,
                            .limit = to >= 0 ? (uint64_t)(to - from) * LX_VIDEO_AUDIO_FRAMES_PER_MS
                                             : UINT64_MAX};
    d->sound.samples = calloc((size_t)VIDEO_AUDIO_FRAMES * LX_VIDEO_AUDIO_CHANNELS, sizeof(float));
    d->sound.tags = malloc(VIDEO_AUDIO_FRAMES * sizeof(uint64_t));
    if (!d->sound.samples || !d->sound.tags)
    {
        free(d->sound.samples);
        free(d->sound.tags);
        memset(&d->sound, 0, sizeof d->sound);
        return 0;
    }
    /* An all-ones tag (UINT64_MAX) marks an empty slot. */
    memset(d->sound.tags, 0xff, VIDEO_AUDIO_FRAMES * sizeof(uint64_t));
    if (!audio_registered)
    {
        if (!GProcessState.registerStereoAudioGenerator ||
            !GProcessState.registerStereoAudioGenerator(fill_audio, NULL, 1))
        {
            free(d->sound.samples);
            free(d->sound.tags);
            memset(&d->sound, 0, sizeof d->sound);
            return 0;
        }
        audio_registered = 1;
    }
    lx_host_monitor_lock(lx_dgv_monitor);
    d->audio = 1;
    lx_host_monitor_unlock(lx_dgv_monitor);
    lx_video_decoder_audio_sink(d->decoder, enqueue_audio, d);
    return 1;
}

void lx_dgv_audio_terminate(void)
{
    if (audio_registered)
    {
        lxUnregisterAudioGenerator(fill_audio, NULL);
        audio_registered = 0;
    }
}
