/* Public domain. No rights reserved. Wholly AI-authored. */
/* Digital-video playback: the worker decodes frames, waits for the media
   clock and submits each frame to the PM owner. */
#include "os2/mm/mmpm/mci/svmc/video.h"
#include "os2/mm/mmpm/mci/mdm/mdm.h"

static void release_frame(void *context)
{
    LxDecodedVideoFrame *frame = context;
    lx_video_frame_release(frame);
    free(frame);
}

void *lx_dgv_play_worker(void *context)
{
    LxDgvDevice *d = context;
    int result = 0;
    int64_t last_end = d->position;
    for (;;)
    {
        lx_host_monitor_lock(lx_dgv_monitor);
        while (!d->stop && d->mode == MCI_MODE_PAUSE && lx_pm_video_ready(d->target))
            lx_host_monitor_wait(lx_dgv_monitor, 20);
        int stopped = d->stop;
        lx_host_monitor_unlock(lx_dgv_monitor);
        if (stopped)
            break;
        if (!lx_pm_video_ready(d->target))
        {
            result = -1;
            break;
        }
        LxDecodedVideoFrame *frame = calloc(1, sizeof *frame);
        if (!frame)
        {
            result = -1;
            break;
        }
        int decoded = lx_video_decoder_next(d->decoder, frame);
        if (decoded <= 0)
        {
            free(frame);
            result = decoded;
            break;
        }
        lx_host_monitor_lock(lx_dgv_monitor);
        while (!d->stop && lx_pm_video_ready(d->target))
        {
            if (d->mode == MCI_MODE_PAUSE)
            {
                lx_host_monitor_wait(lx_dgv_monitor, 20);
                continue;
            }
            int64_t now = lx_dgv_media_clock(d);
            int64_t due = frame->milliseconds;
            if (due <= now)
                break;
            uint64_t wait = (uint64_t)(due - now);
            lx_host_monitor_wait(lx_dgv_monitor, wait > 20 ? 20 : (uint32_t)wait);
        }
        int present = !d->stop && (d->until < 0 || frame->milliseconds < d->until);
        void *target = d->target;
        RECTL destination = d->destination;
        if (present)
        {
            d->position = frame->milliseconds;
            last_end = frame->milliseconds + frame->duration_ms;
        }
        lx_host_monitor_unlock(lx_dgv_monitor);
        if (!present)
        {
            release_frame(frame);
            break;
        }
        if (!lx_pm_video_submit(target, &frame->image, &destination, frame, release_frame))
        {
            result = -1;
            break;
        }
    }
    lx_host_monitor_lock(lx_dgv_monitor);
    if (!result && !d->stop)
    {
        int64_t end = d->until >= 0 && d->until < last_end ? d->until : last_end;
        if (d->audio)
        {
            int64_t audio_end = d->sound.origin / 48 + (int64_t)((d->sound.end + 47) / 48);
            if (d->until >= 0 && audio_end > d->until)
                audio_end = d->until;
            if (audio_end > end)
                end = audio_end;
        }
        while (!d->stop && lx_pm_video_ready(d->target) &&
               (d->mode == MCI_MODE_PAUSE || lx_dgv_media_clock(d) < end))
            lx_host_monitor_wait(lx_dgv_monitor, 10);
        if (!d->stop && !lx_pm_video_ready(d->target))
            result = -1;
        if (!d->stop)
            d->position = end;
    }
    unsigned notify = d->notify;
    uint32_t completion = d->stop      ? d->canceled
                          : result < 0 ? MCI_NOTIFY_ERROR
                                       : MCI_NOTIFY_SUCCESSFUL;
    HWND window = d->notify_window;
    USHORT id = d->id;
    uint32_t user = d->user;
    d->error = result < 0 ? MCIERR_FILE_IO : 0;
    d->mode = MCI_MODE_STOP;
    if (d->audio)
    {
        d->sound.paused = 1;
    }
    lx_host_monitor_wake_all(lx_dgv_monitor);
    lx_host_monitor_unlock(lx_dgv_monitor);
    if (notify && window)
        lx_mci_notify(window, id, (USHORT)completion, user);
    return NULL;
}

void lx_dgv_stop_reason(LxDgvDevice *d, unsigned reason)
{
    lx_host_monitor_lock(lx_dgv_monitor);
    d->stop = 1;
    d->canceled = reason;
    lx_host_monitor_wake_all(lx_dgv_monitor);
    LxHostThread *thread = d->thread;
    d->thread = NULL;
    lx_host_monitor_unlock(lx_dgv_monitor);
    if (thread)
        lx_host_thread_join(thread, NULL);
    lx_host_monitor_lock(lx_dgv_monitor);
    d->audio = 0;
    free(d->sound.samples);
    free(d->sound.tags);
    memset(&d->sound, 0, sizeof d->sound);
    lx_host_monitor_unlock(lx_dgv_monitor);
    lx_video_decoder_audio_sink(d->decoder, NULL, NULL);
    lx_host_monitor_lock(lx_dgv_monitor);
    d->mode = d->decoder ? MCI_MODE_STOP : MCI_MODE_NOT_READY;
    lx_host_monitor_unlock(lx_dgv_monitor);
}

void lx_dgv_stop(LxDgvDevice *d)
{
    lx_dgv_stop_reason(d, MCI_NOTIFY_ABORTED);
}
