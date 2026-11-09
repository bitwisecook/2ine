/* Public domain. No rights reserved. Wholly AI-authored. */
/* mciSendCommand: routes a command to the digital-video, waveaudio or
   sequencer device that owns the device ID. */
#include "os2/mm/mmpm/mci/mdm/mdm.h"
#include "os2/mm/mmpm/mci/admc/admc.h"
#include "os2/mm/mmpm/mci/mimc/mimc.h"
#include "os2/mm/mmpm/mci/mdm/mdm_video.h"

#if TWOINE_PORTABLE_LOADER
size_t lx_mci_parameter_size(USHORT message, ULONG flags)
{
    switch (message)
    {
        case MCI_OPEN:
            return flags & MCI_DGV_OPEN_PARENT ? MCI_DGV_OPEN_PARMS_SIZE : MCI_OPEN_PARMS_SIZE;
        case MCI_MIXSETUP:
            return MCI_MIXSETUP_PARMS_SIZE;
        case MCI_BUFFER:
            return MCI_BUFFER_PARMS_SIZE;
        case MCI_SET:
            return MCI_SET_PARMS_SIZE;
        case MCI_PLAY:
            return flags & MCI_DGV_PLAY_SPEED ? MCI_DGV_PLAY_PARMS_SIZE : MCI_PLAY_PARMS_SIZE;
        case MCI_SEEK:
            return MCI_SEEK_PARMS_SIZE;
        case MCI_LOAD:
            return MCI_LOAD_PARMS_SIZE;
        case MCI_GETDEVCAPS:
            return MCI_GETDEVCAPS_PARMS_SIZE;
        case MCI_STATUS:
            return MCI_STATUS_PARMS_SIZE;
        case MCI_PUT:
        case MCI_WHERE:
            return MCI_VID_RECT_PARMS_SIZE;
        case MCI_WINDOW:
            return MCI_VID_WINDOW_PARMS_SIZE;
        default:
            return MCI_GENERIC_PARMS_SIZE;
    }
}
#endif

ULONG mciSendCommand(USHORT id, USHORT message, ULONG flags, PVOID parameters, USHORT user)
{
    TRACE_NATIVE("mciSendCommand(%u,%u,0x%x,%p,%u)", id, message, (unsigned)flags, parameters,
                 user);
    if (!parameters)
        return MCIERR_INVALID_BUFFER;
    int video_handled = 0;
    ULONG video_result = lx_mci_video_command(id, message, flags, parameters, user, &video_handled);
    if (video_handled)
        return video_result;
    unsigned char *p = parameters;
    lx_mci_devices_initialize();
    if (message == MCI_OPEN)
        return lx_mci_open(flags, p);
    LxMciDevice *d = lx_mci_find_device(id);
    if (!d)
        return MCIERR_INVALID_DEVICE_ID;
    if (message == MCI_MIXSETUP)
        return lx_admc_mixsetup(d, flags, p);
    if (message == MCI_BUFFER)
        return lx_admc_buffer(d, flags, p);
    if (message == MCI_SET && (flags & MCI_SET_VOLUME))
    {
        ULONG level = lx_guest_load_u32(p + MCI_SET_PARMS_LEVEL);
        if (level > 100)
            return MCIERR_INVALID_BUFFER;
        lx_host_monitor_lock(lx_mci_monitor);
        d->volume = level / 100.0f;
        lx_host_monitor_unlock(lx_mci_monitor);
        return 0;
    }
    if (message == MCI_PLAY && d->kind == LX_MCI_SEQUENCER)
        return lx_mimc_play(d, flags, p, user);
    if (message == MCI_PAUSE || message == MCI_RESUME)
    {
        lx_host_monitor_lock(lx_mci_monitor);
        d->paused = message == MCI_PAUSE;
        lx_host_monitor_unlock(lx_mci_monitor);
        return 0;
    }
    if (message == MCI_STOP || message == MCI_CLOSE)
    {
        lx_mci_stop(d, message == MCI_CLOSE);
        return 0;
    }
    return MCIERR_UNSUPPORTED_FUNCTION;
}
