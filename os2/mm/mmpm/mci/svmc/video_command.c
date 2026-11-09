/* Public domain. No rights reserved. Wholly AI-authored. */
/* Digital-video MCI commands. */
#include "os2/mm/mmpm/mci/svmc/video.h"
#include "os2/mm/mmpm/mci/mdm/mdm.h"
#include "emulator/features.h"
#include "host/clock.h"

enum
{
    /* Bytes of the longest device-type name copied from the guest. */
    DEVICE_TYPE_NAME_LIMIT = 128,
    /* An MCI_WAIT play pumps the caller's PM queue this often. */
    WAIT_PUMP_MS = 2
};

static ULONG video_command(USHORT id, USHORT message, ULONG flags, PVOID parameters, USHORT user,
                           int *handled)
{
    *handled = 0;
    unsigned char *p = parameters;
    if (message == MCI_OPEN && p)
    {
        ULONG type = lx_guest_load_u32(p + MCI_OPEN_PARMS_DEVICE_TYPE);
        int is_video = (flags & MCI_OPEN_TYPE_ID) && type == MCI_DEVTYPE_DIGITAL_VIDEO;
        char *text = NULL;
        if (!(flags & MCI_OPEN_TYPE_ID) && type)
        {
            text = lx_execution_copy_string(GProcessState.execution, type, DEVICE_TYPE_NAME_LIMIT);
            is_video =
                text && (!strcasecmp(text, "digitalvideo") || !strcasecmp(text, "digitalvideo01"));
            free(text);
        }
        if (!type && (flags & MCI_OPEN_ELEMENT))
        {
            text = lx_execution_copy_string(GProcessState.execution,
                                            lx_guest_load_u32(p + MCI_OPEN_PARMS_ELEMENT_NAME),
                                            VIDEO_NAME_LIMIT);
            is_video = lx_dgv_video_name(text);
            free(text);
        }
        if (!is_video)
            return 0;
        *handled = 1;
        if (!lxRuntimeFeatureEnabled(LX_FEATURE_VIDEO))
        {
            fprintf(stderr,
                    "2ine: video playback is disabled in this ball's Graphics & Media settings\n");
            return MCIERR_DEVICE_NOT_READY;
        }
        return lx_dgv_initialize() ? lx_dgv_open(flags, p) : MCIERR_OUT_OF_MEMORY;
    }
    LxDgvDevice *d = lx_dgv_find(id);
    if (!d)
        return 0;
    *handled = 1;
    if (!p)
        return MCIERR_INVALID_BUFFER;
    if ((message == MCI_CLOSE || message == MCI_STOP || message == MCI_PAUSE ||
         message == MCI_RESUME) &&
        (flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT)))
        return MCIERR_UNSUPPORTED_FUNCTION;
    if (message == MCI_CLOSE)
    {
        d->closing = 1;
        lx_dgv_stop(d);
        lx_pm_video_release(d->target);
        if (d->frame)
            WinDestroyWindow(d->frame);
        lx_video_decoder_close(d->decoder);
        lx_dgv_reset(d);
        return 0;
    }
    if (message == MCI_STOP)
    {
        lx_dgv_stop(d);
        return 0;
    }
    if (message == MCI_LOAD)
    {
        if (flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT))
            return MCIERR_UNSUPPORTED_FUNCTION;
        LxVideoInfo info;
        LxVideoDecoder *decoder = NULL;
        ULONG error = lx_dgv_load_element(lx_guest_load_u32(p + MCI_LOAD_PARMS_ELEMENT_NAME),
                                          &decoder, &info);
        if (error)
            return error;
        lx_dgv_stop(d);
        lx_video_decoder_close(d->decoder);
        d->decoder = decoder;
        d->info = info;
        d->position = 0;
        d->mode = MCI_MODE_STOP;
        if (!d->window)
            d->destination = (RECTL){0, 0, (LONG)info.width, (LONG)info.height};
        return 0;
    }
    if (message == MCI_WINDOW)
    {
        if (flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_DGV_WINDOW_HWND))
            return MCIERR_UNSUPPORTED_FUNCTION;
        lx_dgv_stop(d);
        return (flags & MCI_DGV_WINDOW_HWND) &&
                       lx_dgv_set_window(d, lx_guest_load_u32(p + MCI_VID_WINDOW_PARMS_HWND_DEST))
                   ? 0
                   : MCIERR_INVALID_BUFFER;
    }
    if (message == MCI_PUT || message == MCI_WHERE)
    {
        /* MCI_WHERE is checked with the MCI_PUT flag values. */
        if ((flags &
             ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_DGV_PUT_RECT | MCI_DGV_PUT_DESTINATION)) ||
            !(flags & MCI_DGV_PUT_DESTINATION))
            return MCIERR_UNSUPPORTED_FUNCTION;
        if (message == MCI_PUT)
        {
            LONG x = (LONG)lx_guest_load_u32(p + MCI_VID_RECT_PARMS_X),
                 y = (LONG)lx_guest_load_u32(p + MCI_VID_RECT_PARMS_Y);
            LONG width = (LONG)lx_guest_load_u32(p + MCI_VID_RECT_PARMS_WIDTH),
                 height = (LONG)lx_guest_load_u32(p + MCI_VID_RECT_PARMS_HEIGHT);
            int64_t right = (int64_t)x + width, top = (int64_t)y + height;
            if (width <= 0 || height <= 0 || right > INT32_MAX || top > INT32_MAX)
                return MCIERR_OUTOFRANGE;
            RECTL rect = {x, y, (LONG)right, (LONG)top};
            lx_host_monitor_lock(lx_dgv_monitor);
            d->destination = rect;
            lx_host_monitor_unlock(lx_dgv_monitor);
        }
        else
        {
            lx_guest_store_u32(p + MCI_VID_RECT_PARMS_X, d->destination.xLeft);
            lx_guest_store_u32(p + MCI_VID_RECT_PARMS_Y, d->destination.yBottom);
            lx_guest_store_u32(p + MCI_VID_RECT_PARMS_WIDTH,
                               d->destination.xRight - d->destination.xLeft);
            lx_guest_store_u32(p + MCI_VID_RECT_PARMS_HEIGHT,
                               d->destination.yTop - d->destination.yBottom);
        }
        return 0;
    }
    if (message == MCI_SET && (flags & MCI_SET_VOLUME))
    {
        if ((flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_SET_VOLUME | MCI_SET_AUDIO)) ||
            lx_guest_load_u32(p + MCI_SET_PARMS_AUDIO) != MCI_SET_AUDIO_ALL ||
            lx_guest_load_u32(p + MCI_SET_PARMS_OVER))
            return MCIERR_UNSUPPORTED_FUNCTION;
        ULONG level = lx_guest_load_u32(p + MCI_SET_PARMS_LEVEL);
        if (level > 100)
            return MCIERR_OUTOFRANGE;
        lx_host_monitor_lock(lx_dgv_monitor);
        d->volume = level / 100.0f;
        d->sound.volume = d->volume;
        lx_host_monitor_unlock(lx_dgv_monitor);
        return 0;
    }
    if (message == MCI_SET)
    {
        if ((flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_SET_TIME_FORMAT)) ||
            !(flags & MCI_SET_TIME_FORMAT))
            return MCIERR_UNSUPPORTED_FUNCTION;
        ULONG format = lx_guest_load_u32(p + MCI_SET_PARMS_TIME_FORMAT);
        if (format != MCI_FORMAT_MILLISECONDS && format != MCI_FORMAT_FRAMES)
            return MCIERR_INVALID_FLAG;
        lx_host_monitor_lock(lx_dgv_monitor);
        d->format = format;
        lx_host_monitor_unlock(lx_dgv_monitor);
        return 0;
    }
    if (message == MCI_STATUS || message == MCI_GETDEVCAPS)
    {
        /* MCI_STATUS_ITEM and MCI_GETDEVCAPS_MESSAGE share a value, as do the
           items and return fields of both parameter blocks. */
        ULONG kind = flags & (MCI_GETDEVCAPS_MESSAGE | MCI_GETDEVCAPS_ITEM);
        if ((flags &
             ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_GETDEVCAPS_MESSAGE | MCI_GETDEVCAPS_ITEM)) ||
            (message == MCI_STATUS && kind != MCI_STATUS_ITEM) ||
            (message == MCI_GETDEVCAPS && kind != MCI_GETDEVCAPS_MESSAGE &&
             kind != MCI_GETDEVCAPS_ITEM))
            return MCIERR_INVALID_FLAG;
        ULONG item = lx_guest_load_u32(p + MCI_STATUS_PARMS_ITEM), value = 0;
        if (message == MCI_GETDEVCAPS && (flags & MCI_GETDEVCAPS_MESSAGE))
        {
            switch (lx_guest_load_u16(p + MCI_GETDEVCAPS_PARMS_MESSAGE))
            {
                case MCI_OPEN:
                case MCI_CLOSE:
                case MCI_PLAY:
                case MCI_SEEK:
                case MCI_STOP:
                case MCI_PAUSE:
                case MCI_GETDEVCAPS:
                case MCI_STATUS:
                case MCI_SET:
                case MCI_LOAD:
                case MCI_RESUME:
                case MCI_PUT:
                case MCI_WHERE:
                case MCI_WINDOW:
                    value = 1;
                    break;
            }
            lx_guest_store_u32(p + MCI_GETDEVCAPS_PARMS_RETURN, value);
            return 0;
        }
        lx_host_monitor_lock(lx_dgv_monitor);
        if (message == MCI_STATUS)
        {
            switch (item)
            {
                case MCI_STATUS_LENGTH:
                    value = lx_dgv_position_units(d, d->info.duration_ms);
                    break;
                case MCI_STATUS_MODE:
                    value = d->mode;
                    break;
                case MCI_STATUS_POSITION:
                    value = lx_dgv_position_units(d, d->position);
                    break;
                case MCI_STATUS_MEDIA_PRESENT:
                case MCI_STATUS_READY:
                case MCI_STATUS_VIDEO:
                    value = d->decoder != NULL;
                    break;
                case MCI_STATUS_VOLUME:
                    value = (ULONG)(d->volume * 100.0f + 0.5f);
                    break;
                case MCI_STATUS_TIME_FORMAT:
                    value = d->format;
                    break;
                case MCI_STATUS_AUDIO:
                    value = d->info.audio_present;
                    break;
                case MCI_DGV_STATUS_HWND:
                    value = d->window;
                    break;
                case MCI_DGV_STATUS_VIDEO_X_EXTENT:
                    value = d->info.width;
                    break;
                case MCI_DGV_STATUS_VIDEO_Y_EXTENT:
                    value = d->info.height;
                    break;
                default:
                    lx_host_monitor_unlock(lx_dgv_monitor);
                    return MCIERR_UNSUPPORTED_FUNCTION;
            }
        }
        else
        {
            switch (item)
            {
                case MCI_GETDEVCAPS_CAN_PLAY:
                case MCI_GETDEVCAPS_CAN_SETVOLUME:
                case MCI_GETDEVCAPS_HAS_VIDEO:
                case MCI_GETDEVCAPS_USES_FILES:
                case MCI_DGV_GETDEVCAPS_CAN_STRETCH:
                case MCI_DGV_GETDEVCAPS_CAN_DISTORT:
                    value = 1;
                    break;
                case MCI_GETDEVCAPS_CAN_EJECT:
                case MCI_GETDEVCAPS_CAN_LOCKEJECT:
                case MCI_GETDEVCAPS_CAN_PROCESS_INTERNAL:
                case MCI_GETDEVCAPS_CAN_RECORD:
                case MCI_GETDEVCAPS_CAN_RECORD_INSERT:
                case MCI_GETDEVCAPS_CAN_SAVE:
                case MCI_GETDEVCAPS_CAN_STREAM:
                case MCI_GETDEVCAPS_HAS_IMAGE:
                case MCI_GETDEVCAPS_CAN_CLOSE_DOOR:
                case MCI_VID_GETDEVCAPS_CAN_FREEZE:
                case MCI_DGV_GETDEVCAPS_OVERLAY_GRAPHICS:
                case MCI_DGV_GETDEVCAPS_CAN_REVERSE:
                    value = 0;
                    break;
                case MCI_DGV_GETDEVCAPS_MAX_WINDOWS:
                    value = 1;
                    break;
                case MCI_GETDEVCAPS_DEVICE_TYPE:
                    value = MCI_DEVTYPE_DIGITAL_VIDEO;
                    break;
                case MCI_GETDEVCAPS_HAS_AUDIO:
                    value = d->info.audio_present;
                    break;
                case MCI_DGV_GETDEVCAPS_VIDEO_X_EXTENT:
                case MCI_DGV_GETDEVCAPS_IMAGE_X_EXTENT:
                    value = d->info.width;
                    break;
                case MCI_DGV_GETDEVCAPS_VIDEO_Y_EXTENT:
                case MCI_DGV_GETDEVCAPS_IMAGE_Y_EXTENT:
                    value = d->info.height;
                    break;
                default:
                    lx_host_monitor_unlock(lx_dgv_monitor);
                    return MCIERR_UNSUPPORTED_FUNCTION;
            }
        }
        lx_host_monitor_unlock(lx_dgv_monitor);
        lx_guest_store_u32(p + MCI_STATUS_PARMS_RETURN, value);
        return 0;
    }
    if (message == MCI_PAUSE || message == MCI_RESUME)
    {
        lx_host_monitor_lock(lx_dgv_monitor);
        if (message == MCI_PAUSE && d->mode == MCI_MODE_PLAY)
        {
            d->mode = MCI_MODE_PAUSE;
            d->paused_at = lx_host_video_ticks_ms();
            if (d->audio)
            {
                d->sound.paused = 1;
            }
        }
        else if (message == MCI_RESUME && d->mode == MCI_MODE_PAUSE)
        {
            d->started += lx_host_video_ticks_ms() - d->paused_at;
            d->mode = MCI_MODE_PLAY;
            if (d->audio)
            {
                d->sound.paused = 0;
            }
            lx_host_monitor_wake_all(lx_dgv_monitor);
        }
        else
        {
            lx_host_monitor_unlock(lx_dgv_monitor);
            return MCIERR_DEVICE_NOT_READY;
        }
        lx_host_monitor_unlock(lx_dgv_monitor);
        return 0;
    }
    if (message == MCI_SEEK)
    {
        if ((flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_TO_START | MCI_TO_END)) ||
            (flags & (MCI_TO_START | MCI_TO_END)) == (MCI_TO_START | MCI_TO_END))
            return MCIERR_UNSUPPORTED_FUNCTION;
        if (!d->decoder)
            return MCIERR_DEVICE_NOT_READY;
        lx_dgv_stop(d);
        int64_t at = flags & MCI_TO_START ? 0
                     : flags & MCI_TO_END
                         ? d->info.duration_ms
                         : lx_dgv_milliseconds(d, lx_guest_load_u32(p + MCI_SEEK_PARMS_TO));
        if (at < 0 || (d->info.duration_ms > 0 && at > d->info.duration_ms))
            return MCIERR_OUTOFRANGE;
        if (!lx_video_decoder_seek(d->decoder, at))
            return MCIERR_FILE_IO;
        d->position = at;
        return 0;
    }
    if (message == MCI_PLAY)
    {
        if (flags & ~((ULONG)MCI_NOTIFY | MCI_WAIT | MCI_FROM | MCI_TO))
            return MCIERR_UNSUPPORTED_FUNCTION;
        if (!d->decoder)
            return MCIERR_DEVICE_NOT_READY;
        lx_dgv_stop_reason(d, MCI_NOTIFY_SUPERSEDED);
        if (!d->target && !lx_dgv_create_window(d))
            return MCIERR_DEVICE_NOT_READY;
        int64_t from = flags & MCI_FROM
                           ? lx_dgv_milliseconds(d, lx_guest_load_u32(p + MCI_PLAY_PARMS_FROM))
                           : d->position;
        int64_t to = flags & MCI_TO
                         ? lx_dgv_milliseconds(d, lx_guest_load_u32(p + MCI_PLAY_PARMS_TO))
                     : d->info.duration_ms > 0 ? d->info.duration_ms
                                               : -1;
        if (from < 0 || ((flags & MCI_TO) && to < 0) || (to >= 0 && to <= from) ||
            (d->info.duration_ms > 0 && (from > d->info.duration_ms || to > d->info.duration_ms)))
            return MCIERR_OUTOFRANGE;
        if (!lx_video_decoder_seek(d->decoder, from))
            return MCIERR_FILE_IO;
        if (!lx_dgv_start_audio(d, from, to))
            return MCIERR_DEVICE_NOT_READY;
        lx_host_monitor_lock(lx_dgv_monitor);
        d->position = from;
        d->until = to;
        d->stop = 0;
        d->error = 0;
        d->mode = MCI_MODE_PLAY;
        d->notify = flags & MCI_NOTIFY;
        d->notify_window = lx_guest_load_u32(p + MCI_PARMS_CALLBACK);
        d->user = user;
        d->started = lx_host_video_ticks_ms() - (uint64_t)from;
        d->thread = lx_host_thread_create(lx_dgv_play_worker, d, NULL);
        if (d->audio && d->thread)
        {
            d->sound.paused = 0;
        }
        if (!d->thread)
        {
            d->mode = MCI_MODE_STOP;
            lx_host_monitor_unlock(lx_dgv_monitor);
            lx_dgv_stop(d);
            return MCIERR_OUT_OF_MEMORY;
        }
        lx_host_monitor_unlock(lx_dgv_monitor);
        if (flags & MCI_WAIT)
        {
            HAB hab = WinQueryAnchorBlock(d->window);
            for (;;)
            {
                lx_host_monitor_lock(lx_dgv_monitor);
                int playing = d->mode != MCI_MODE_STOP && d->id == id;
                lx_host_monitor_unlock(lx_dgv_monitor);
                lx_pm_video_pump_current();
                if (!playing)
                    break;
                QMSG message;
                while (WinPeekMsg(hab, &message, 0, 0, 0, PM_REMOVE))
                    WinDispatchMsg(hab, &message);
                lx_host_sleep_ms(WAIT_PUMP_MS);
            }
            lx_dgv_stop(d);
            return lx_pm_video_ready(d->target) ? d->error : MCIERR_FILE_IO;
        }
        return 0;
    }
    return MCIERR_UNSUPPORTED_FUNCTION;
}

ULONG lx_mci_video_command(USHORT id, USHORT message, ULONG flags, PVOID parameters, USHORT user,
                           int *handled)
{
    ULONG result = video_command(id, message, flags, parameters, user, handled);
    if (*handled && !result && (flags & MCI_NOTIFY) && message != MCI_PLAY && parameters)
    {
        ULONG window = lx_guest_load_u32((uint8 *)parameters + MCI_PARMS_CALLBACK);
        USHORT device = message == MCI_OPEN
                            ? lx_guest_load_u16((uint8 *)parameters + MCI_OPEN_PARMS_DEVICE_ID)
                            : id;
        if (window)
            lx_mci_notify(window, device, MCI_NOTIFY_SUCCESSFUL, user);
    }
    return result;
}

void lx_mci_video_shutdown(void)
{
    if (!lx_dgv_monitor)
        return;
    lx_dgv_audio_terminate();
    for (unsigned i = 0; i < VIDEO_DEVICES; i++)
        if (lx_dgv_devices[i].id)
        {
            LxDgvDevice *d = &lx_dgv_devices[i];
            d->closing = 1;
            lx_dgv_stop(d);
            lx_pm_video_release(d->target);
            if (d->frame)
                WinDestroyWindow(d->frame);
            lx_video_decoder_close(d->decoder);
            lx_dgv_reset(d);
        }
}
