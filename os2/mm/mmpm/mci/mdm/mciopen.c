/* Public domain. No rights reserved. Wholly AI-authored. */
/* MCI_OPEN for the waveaudio (DART amp-mixer) and sequencer devices. */
#include "os2/mm/mmpm/mci/mdm/mdm.h"
#include "os2/mm/mmpm/mci/mimc/mimc.h"
#include "emulator/features.h"

static USHORT next_id = 1;

ULONG lx_mci_open(ULONG flags, unsigned char *p)
{
    LxMciDeviceKind kind = 0;
    if ((flags & MCI_OPEN_TYPE_ID) &&
        lx_guest_load_u32(p + MCI_OPEN_PARMS_DEVICE_TYPE) == MCI_DEVTYPE_AUDIO_AMPMIX)
        kind = LX_MCI_WAVEAUDIO;
    else if (flags & MCI_OPEN_MMIO)
        kind = LX_MCI_SEQUENCER;
    if (!kind)
        return MCIERR_DEVICE_NOT_READY;
    if (!lxRuntimeFeatureEnabled(kind == LX_MCI_WAVEAUDIO ? LX_FEATURE_PCM : LX_FEATURE_MIDI))
    {
        fprintf(stderr, "2ine: %s playback is disabled in this ball's Graphics & Media settings\n",
                kind == LX_MCI_WAVEAUDIO ? "PCM audio" : "MIDI");
        return MCIERR_DEVICE_NOT_READY;
    }
    lx_host_monitor_lock(lx_mci_monitor);
    LxMciDevice *d = NULL;
    for (unsigned i = 0; i < MCI_DEVICE_COUNT; i++)
        if (!lx_mci_devices[i].id)
        {
            d = &lx_mci_devices[i];
            break;
        }
    if (!d || !next_id || next_id >= MCI_DEVICE_ID_LIMIT)
    {
        lx_host_monitor_unlock(lx_mci_monitor);
        return MCIERR_OUT_OF_MEMORY;
    }
    memset(d, 0, sizeof *d);
    d->id = next_id++;
    d->kind = kind;
    d->volume = 1;
    d->stopped = 1;
    if (kind == LX_MCI_SEQUENCER)
    {
        /* With MCI_OPEN_MMIO the element name is an open HMMIO. */
        ULONG error = lx_mimc_open(d, lx_guest_load_u32(p + MCI_OPEN_PARMS_ELEMENT_NAME));
        if (error)
        {
            memset(d, 0, sizeof *d);
            lx_host_monitor_unlock(lx_mci_monitor);
            return error;
        }
    }
    lx_guest_store_u16(p + MCI_OPEN_PARMS_DEVICE_ID, d->id);
    lx_host_monitor_unlock(lx_mci_monitor);
    return 0;
}
