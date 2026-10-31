/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"
int DosBeepGenerator(void *userdata, float *stream, int samples, int freq)
{
    const float beep_volume = GLoaderState.beep_volume;
    DosBeepGeneratorInfo *info = &dos_beep_info;
    assert(userdata == &dos_beep_info);

    while (samples > 0)
    {
        if (!info->samples_remaining)
        {
            if (!info->next_freq)
            {
                return 0;
            }

            info->square_length = ((ULONG)freq) / info->next_freq;
            info->square_remainder = ((ULONG)freq) % info->next_freq;
            info->samples_remaining = ((ULONG)((info->next_duration / 1000.0) * ((ULONG)freq))) * 1;
            info->accumulator = 0;
            info->next_freq = 0;
            info->next_duration = 0;
        }
        else if (!info->square_remaining)
        {
            info->positive = !info->positive;
            info->square_remaining = info->square_length;
            info->accumulator += info->square_remainder;
            if (info->accumulator >= info->square_length)
            {
                info->accumulator -= info->square_length;
                info->square_remaining++;
            }
        }
        else
        { /* Mix the current square-wave phase at PC speaker amplitude. */
            const float val = info->positive ? beep_volume : -beep_volume;
            int total = info->square_remaining;
            if (info->samples_remaining < total)
                total = info->samples_remaining;
            if (samples < total)
                total = samples;

            FIXME("good candidate for SSE");
            for (int i = 0; i < total; i++)
            {
                stream[i] += val;
            }

            samples -= total;
            info->samples_remaining -= total;
            info->square_remaining -= total;
            stream += total;
        }
    }

    return 1;
}

APIRET DosBeep_implementation(ULONG freq, ULONG duration)
{
    if ((freq < 0x25) || (freq > 0x7FFF))
    {
        return ERROR_INVALID_FREQUENCY;
    }
    else if (duration == 0)
    {
        return NO_ERROR;
    }

    /* Round duration to the 55-ms PC speaker tick. */
    FIXME("sanity check this");
    const ULONG duration_diff = duration % 55;
    const ULONG mix_duration = duration + (duration_diff ? (55 - duration_diff) : 0);

    /* Return before playback ends so consecutive calls can queue without a gap. */

    /* Serialize speaker use so concurrent DosBeep calls do not overlap. */
    grabLock(&GMutexDosBeep);

    FIXME("should be atomic");
    while (*((volatile ULONG *)&dos_beep_info.next_freq) != 0)
    {
        lx_host_sleep_ms(1);
    }

    dos_beep_info.next_duration = mix_duration;
    dos_beep_info.next_freq = freq;

    /* Leave one audio tick queued to bridge scheduler delays between consecutive calls. */
    if (duration > 55)
    {
        lx_host_sleep_ms(duration - 55);
    }

    const int rc = GLoaderState.registerAudioGenerator(DosBeepGenerator, &dos_beep_info, 1);
    if (!rc)
    {
        memset(&dos_beep_info, '\0', sizeof(dos_beep_info));
    }

    ungrabLock(&GMutexDosBeep);

    return NO_ERROR;
}

APIRET DosBeep(ULONG freq, ULONG duration)
{
    TRACE_NATIVE("DosBeep(%u, %u)", freq, duration);
    return DosBeep_implementation(freq, duration);
}

APIRET16 Dos16Beep(USHORT freq, USHORT duration)
{
    TRACE_NATIVE("Dos16Beep(%u, %u)", freq, duration);
    return DosBeep_implementation(freq, duration);
}
