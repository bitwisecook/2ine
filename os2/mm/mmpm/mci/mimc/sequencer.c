/* Public domain. No rights reserved. Wholly AI-authored. */
/* The sequencer device: a standard MIDI file played through the synthesizer,
   or through the oscillator in builds without one. */
#include "os2/mm/mmpm/mci/mimc/mimc.h"
#include <math.h>

ULONG lx_mimc_open(LxMciDevice *d, ULONG hmmio)
{
    size_t size = 0;
    unsigned char *data = lx_mmio_copy(hmmio, &size);
    int ok = data && lx_midi_sequence_parse(&d->sequence, data, size);
    free(data);
#if TWOINE_MIDI_SYNTH || TWOINE_MT32_SYNTH
    if (ok)
    {
        d->synth = lx_midi_synth_create();
        if (!d->synth)
        {
            lx_midi_sequence_clear(&d->sequence);
            return MCIERR_DEVICE_NOT_READY;
        }
    }
#else
    fprintf(stderr, "2ine: basic oscillator MIDI fallback selected at build time.\n");
#endif
    if (!ok)
    {
        lx_midi_sequence_clear(&d->sequence);
        return MCIERR_INVALID_BUFFER;
    }
    return 0;
}

ULONG lx_mimc_play(LxMciDevice *d, ULONG flags, unsigned char *p, USHORT user)
{
    lx_host_monitor_lock(lx_mci_monitor);
#if TWOINE_MIDI_SYNTH || TWOINE_MT32_SYNTH
    if (!lx_midi_synth_reset(d->synth))
    {
        lx_host_monitor_unlock(lx_mci_monitor);
        return MCIERR_OUT_OF_MEMORY;
    }
#endif
    d->time = 0;
    d->next_event = 0;
    memset(&d->oscillator, 0, sizeof d->oscillator);
    d->paused = 0;
    d->notify = flags & MCI_NOTIFY;
    d->notify_window = lx_guest_load_u32(p + MCI_PARMS_CALLBACK);
    d->notify_user = user;
    d->ending = 0;
    lx_host_monitor_unlock(lx_mci_monitor);
    return lx_mci_start_audio(d);
}

static void midi_event(LxMciDevice *d, const LxMidiEvent *e)
{
#if TWOINE_MIDI_SYNTH || TWOINE_MT32_SYNTH
    if (e->sysex)
        lx_midi_synth_sysex(d->synth, e->sysex, e->sysex_size);
    else if (!e->tempo)
        lx_midi_synth_event(d->synth, e->status, e->key, e->value);
    return;
#endif
    lx_midi_oscillator_event(&d->oscillator, e->status, e->key, e->value);
}

void lx_mimc_render(LxMciDevice *d, float *samples, int count, int frequency)
{
    if (d->time >= d->sequence.duration)
        return;
    for (int n = 0; n < count && d->time < d->sequence.duration;)
    {
        while (d->next_event < d->sequence.count &&
               d->sequence.events[d->next_event].time <= d->time)
            midi_event(d, &d->sequence.events[d->next_event++]);
        double boundary = d->sequence.duration;
        if (d->next_event < d->sequence.count && d->sequence.events[d->next_event].time < boundary)
            boundary = d->sequence.events[d->next_event].time;
        int block = count - n;
        double remaining = ceil((boundary - d->time) * frequency);
        if (remaining < block)
            block = remaining > 1 ? (int)remaining : 1;
#if TWOINE_MIDI_SYNTH || TWOINE_MT32_SYNTH
        lx_midi_synth_render_stereo(d->synth, samples + 2 * n, (unsigned)block, (unsigned)frequency,
                                    d->volume);
#else
        lx_midi_oscillator_render_stereo(&d->oscillator, samples + 2 * n, (unsigned)block,
                                         (unsigned)frequency, d->volume);
#endif
        n += block;
        d->time += (double)block / frequency;
    }
    if (d->time >= d->sequence.duration)
    {
        d->ending = 1;
        lx_host_monitor_wake_all(lx_mci_monitor);
    }
}

void lx_mimc_close(LxMciDevice *d)
{
    lx_midi_sequence_clear(&d->sequence);
    lx_midi_synth_destroy(d->synth);
    d->synth = NULL;
}
