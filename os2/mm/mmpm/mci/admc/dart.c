/* Public domain. No rights reserved. Wholly AI-authored. */
/* The waveaudio device's DART interface. Queued buffers keep their PCM until
   playback completes; MIX_WRITE_COMPLETE runs on the delivery thread. */
#include "os2/mm/mmpm/mci/admc/admc.h"

/* DART limits: bytes per buffer; MCI_MIXSETUP suggests three 4 KiB buffers. */
enum
{
    MIX_BUFFER_BYTES_MAX = 4 * 1024 * 1024,
    MIX_SUGGESTED_BUFFER_SIZE = 4096,
    MIX_SUGGESTED_BUFFERS = 3,
    MIX_RATE_MIN = 4000,
    MIX_RATE_MAX = 192000,
    MIX_CHANNELS_MAX = 2,
    /* MCI_ALLOCATE_MEMORY buffers occupy whole guest pages. */
    GUEST_PAGE_SIZE = 4096,
    /* 8-bit PCM is unsigned, centred on 128; 16-bit PCM is signed little-endian. */
    PCM8_MIDPOINT = 128
};

static const float PCM8_FULL_SCALE = 128.0f, PCM16_FULL_SCALE = 32768.0f;

#if !TWOINE_PORTABLE_LOADER
static LONG OS2API mixWrite(ULONG handle, PVOID records, ULONG count)
{
    return lx_admc_queue_buffers(handle, (ULONG)(uintptr_t)records, count);
}
#endif

static void free_packets(LxMixPacket *p)
{
    while (p)
    {
        LxMixPacket *next = p->next;
        free(p->samples);
        free(p);
        p = next;
    }
}

ULONG lx_admc_mixsetup(LxMciDevice *d, ULONG flags, unsigned char *p)
{
    if (flags & MCI_MIXSETUP_DEINIT)
    {
        lx_mci_stop(d, 0);
        return 0;
    }
    if (!(flags & MCI_MIXSETUP_INIT))
        return MCIERR_UNSUPPORTED_FUNCTION;
    ULONG bits = lx_guest_load_u32(p + MCI_MIXSETUP_PARMS_BITS_PER_SAMPLE),
          channels = lx_guest_load_u32(p + MCI_MIXSETUP_PARMS_CHANNELS),
          rate = lx_guest_load_u32(p + MCI_MIXSETUP_PARMS_SAMPLES_PER_SEC);
    if (d->kind != LX_MCI_WAVEAUDIO || (bits != 8 && bits != 16) || !channels ||
        channels > MIX_CHANNELS_MAX || rate < MIX_RATE_MIN || rate > MIX_RATE_MAX ||
        lx_guest_load_u32(p + MCI_MIXSETUP_PARMS_FORMAT_TAG) != MCI_WAVE_FORMAT_PCM ||
        lx_guest_load_u32(p + MCI_MIXSETUP_PARMS_FORMAT_MODE) != MCI_PLAY)
        return MCIERR_INVALID_BUFFER;
    lx_host_monitor_lock(lx_mci_monitor);
    d->bits = bits;
    d->channels = channels;
    d->rate = rate;
    d->callback = lx_guest_load_u32(p + MCI_MIXSETUP_PARMS_MIX_EVENT);
    lx_host_monitor_unlock(lx_mci_monitor);
#if TWOINE_PORTABLE_LOADER
    ULONG write_entry = lx_admc_write_entry();
    if (!write_entry)
        return MCIERR_OUT_OF_MEMORY;
    lx_guest_store_u32(p + MCI_MIXSETUP_PARMS_MIX_WRITE, write_entry);
#else
    lx_guest_store_u32(p + MCI_MIXSETUP_PARMS_MIX_WRITE, (ULONG)(uintptr_t)mixWrite);
#endif
    lx_guest_store_u32(p + MCI_MIXSETUP_PARMS_MIX_HANDLE, d->id);
    lx_guest_store_u32(p + MCI_MIXSETUP_PARMS_BUFFER_SIZE, MIX_SUGGESTED_BUFFER_SIZE);
    lx_guest_store_u32(p + MCI_MIXSETUP_PARMS_NUM_BUFFERS, MIX_SUGGESTED_BUFFERS);
    return lx_mci_start_audio(d);
}

static void unmap_buffers(LxMciDevice *d, unsigned count)
{
    for (unsigned i = 0; i < count; i++)
        lx_execution_unmap(GProcessState.execution, d->buffers[i]);
    memset(d->buffers, 0, sizeof d->buffers);
}

ULONG lx_admc_buffer(LxMciDevice *d, ULONG flags, unsigned char *p)
{
    ULONG count = lx_guest_load_u32(p + MCI_BUFFER_PARMS_NUM_BUFFERS),
          size = lx_guest_load_u32(p + MCI_BUFFER_PARMS_BUFFER_SIZE),
          list = lx_guest_load_u32(p + MCI_BUFFER_PARMS_BUFFER_LIST);
    if (!count || count > MIX_BUFFER_MAX || !size || size > MIX_BUFFER_BYTES_MAX)
        return MCIERR_INVALID_BUFFER;
    if (flags & MCI_ALLOCATE_MEMORY)
    {
        if (d->buffer_count)
            return MCIERR_INVALID_BUFFER;
        unsigned char records[MIX_BUFFER_MAX * MCI_MIX_BUFFER_SIZE] = {0};
        for (ULONG i = 0; i < count; i++)
        {
            d->buffers[i] = lx_execution_map(GProcessState.execution, 0,
                                             (size + GUEST_PAGE_SIZE - 1) & ~(GUEST_PAGE_SIZE - 1u),
                                             LX_GUEST_READ | LX_GUEST_WRITE, 0);
            if (!d->buffers[i])
            {
                unmap_buffers(d, i);
                return MCIERR_OUT_OF_MEMORY;
            }
            unsigned char *record = records + i * MCI_MIX_BUFFER_SIZE;
            lx_guest_store_u32(record + MCI_MIX_BUFFER_STRUCT_LENGTH, MCI_MIX_BUFFER_SIZE);
            lx_guest_store_u32(record + MCI_MIX_BUFFER_BUFFER,
                               lx_execution_mapping_address(d->buffers[i]));
            lx_guest_store_u32(record + MCI_MIX_BUFFER_BUFFER_LENGTH, size);
        }
        if (!lx_execution_write(GProcessState.execution, list, records,
                                count * MCI_MIX_BUFFER_SIZE))
        {
            unmap_buffers(d, count);
            return MCIERR_INVALID_BUFFER;
        }
        d->buffer_count = count;
        return 0;
    }
    if (flags & MCI_DEALLOCATE_MEMORY)
    {
        /* Retire the stream before releasing caller-visible buffers. */
        lx_mci_stop(d, 0);
        unmap_buffers(d, d->buffer_count);
        d->buffer_count = 0;
        return 0;
    }
    return MCIERR_UNSUPPORTED_FUNCTION;
}

LONG lx_admc_queue_buffers(ULONG handle, ULONG address, ULONG count)
{
    if (!count || count > MIX_BUFFER_MAX)
        return MCIERR_INVALID_BUFFER;
    lx_host_monitor_lock(lx_mci_monitor);
    LxMciDevice *d = lx_mci_find_device(handle);
    if (!d || d->kind != LX_MCI_WAVEAUDIO || !d->rate)
    {
        lx_host_monitor_unlock(lx_mci_monitor);
        return MCIERR_INVALID_DEVICE_ID;
    }
    if (d->stopped || count > MIX_BUFFER_MAX - d->pending)
    {
        lx_host_monitor_unlock(lx_mci_monitor);
        return MCIERR_DEVICE_NOT_READY;
    }
    LxMixPacket *head = NULL, *tail = NULL;
    unsigned submitted = 0;
    for (ULONG i = 0; i < count; i++)
    {
        unsigned char record[MCI_MIX_BUFFER_SIZE];
        if (!lx_execution_read(GProcessState.execution, address + i * MCI_MIX_BUFFER_SIZE, record,
                               MCI_MIX_BUFFER_SIZE))
            goto fail;
        ULONG length = lx_guest_load_u32(record + MCI_MIX_BUFFER_BUFFER_LENGTH),
              data = lx_guest_load_u32(record + MCI_MIX_BUFFER_BUFFER);
        if (length > MIX_BUFFER_BYTES_MAX)
            goto fail;
        if (!length)
            continue;
        unsigned bytes_per_frame = d->channels * (d->bits / 8);
        ULONG frames = length / bytes_per_frame;
        if (!frames)
            continue;
        unsigned char *pcm = malloc(length);
        LxMixPacket *p = calloc(1, sizeof *p);
        if (!pcm || !p)
        {
            free(pcm);
            free(p);
            goto fail;
        }
        p->samples = malloc(frames * MCI_MIXER_CHANNELS * sizeof(float));
        p->count = frames;
        p->record = address + i * MCI_MIX_BUFFER_SIZE;
        if (!p->samples || !lx_execution_read(GProcessState.execution, data, pcm, length))
        {
            free(p->samples);
            free(p);
            free(pcm);
            goto fail;
        }
        for (ULONG frame = 0; frame < frames; frame++)
        {
            for (ULONG c = 0; c < MCI_MIXER_CHANNELS; c++)
            {
                /* Mono buffers feed both output channels. */
                ULONG off = (frame * d->channels + (d->channels == 1 ? 0 : c)) * (d->bits / 8);
                p->samples[frame * MCI_MIXER_CHANNELS + c] =
                    d->bits == 8 ? ((int)pcm[off] - PCM8_MIDPOINT) / PCM8_FULL_SCALE
                                 : (int16_t)(pcm[off] | pcm[off + 1] << 8) / PCM16_FULL_SCALE;
            }
        }
        free(pcm);
        if (tail)
            tail->next = p;
        else
            head = p;
        tail = p;
        submitted++;
    }
    d->pending += submitted;
    if (head)
    {
        if (d->tail)
            d->tail->next = head;
        else
            d->head = head;
        d->tail = tail;
    }
    lx_host_monitor_unlock(lx_mci_monitor);
    return 0;
fail:
    free_packets(head);
    lx_host_monitor_unlock(lx_mci_monitor);
    return MCIERR_INVALID_BUFFER;
}

void lx_admc_render(LxMciDevice *d, float *samples, int count, int frequency)
{
    for (int n = 0; n < count; n++)
    {
        if (d->kind == LX_MCI_WAVEAUDIO && d->head)
        {
            LxMixPacket *p = d->head;
            size_t source = (size_t)p->position * MCI_MIXER_CHANNELS;
            samples[MCI_MIXER_CHANNELS * n] += p->samples[source] * d->volume;
            samples[MCI_MIXER_CHANNELS * n + 1] += p->samples[source + 1] * d->volume;
            p->position += (double)d->rate / frequency;
            if (p->position >= p->count)
            {
                d->head = p->next;
                if (!d->head)
                    d->tail = NULL;
                p->next = NULL;
                if (d->done_tail)
                    d->done_tail->next = p;
                else
                    d->done = p;
                d->done_tail = p;
                lx_host_monitor_wake_all(lx_mci_monitor);
            }
        }
    }
}

int lx_admc_deliver(LxMciDevice *d)
{
    LxMixPacket *p = d->done;
    if (!p)
        return 0;
    d->done = p->next;
    if (!d->done)
        d->done_tail = NULL;
    if (d->pending)
        d->pending--;
    ULONG callback = d->callback;
    lx_host_monitor_unlock(lx_mci_monitor);
    if (callback)
        lx_execution_callback32_4(GProcessState.execution, callback, 0, p->record,
                                  MIX_WRITE_COMPLETE, 0);
    free(p->samples);
    free(p);
    lx_host_monitor_lock(lx_mci_monitor);
    return 1;
}

void lx_admc_discard(LxMciDevice *d, int closing)
{
    free_packets(d->head);
    free_packets(d->done);
    d->head = d->tail = d->done = d->done_tail = NULL;
    d->pending = 0;
    if (closing)
        for (unsigned i = 0; i < d->buffer_count; i++)
            lx_execution_unmap(GProcessState.execution, d->buffers[i]);
}
