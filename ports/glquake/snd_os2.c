/* Public domain. No rights reserved.
 * Quake DMA ring backed by OS/2 MMPM/2 DART stereo PCM buffers.
 * The high-priority completion callback copies only already mixed ring samples.
 * MCI_STOP with MCI_WAIT retires callbacks before any buffer is released.
 */
#define INCL_DOS
#define INCL_OS2MM
#include <os2.h>
#include <os2me.h>
#include "quakedef.h"

#define DMA_FRAMES 16384
#define DART_FRAMES 512
#define DART_BUFFERS 2
#define PCM_CHANNELS 2
#define PCM_BITS 16

static MCI_MIXSETUP_PARMS mixer;
static MCI_BUFFER_PARMS allocation;
static MCI_MIX_BUFFER buffers[DART_BUFFERS];
static USHORT device;
static volatile ULONG completed_frames;
static ULONG queued_frames;
static volatile int running;
static int allocated, started;
static dma_t dma;

static void FillBuffer(PMCI_MIX_BUFFER buffer)
{
    ULONG offset = queued_frames & (DMA_FRAMES - 1);
    ULONG first = DMA_FRAMES - offset;
    if (first > DART_FRAMES)
        first = DART_FRAMES;
    memcpy(buffer->pBuffer, dma.buffer + offset * PCM_CHANNELS * 2, first * PCM_CHANNELS * 2);
    if (first < DART_FRAMES)
        memcpy((byte *)buffer->pBuffer + first * PCM_CHANNELS * 2, dma.buffer,
               (DART_FRAMES - first) * PCM_CHANNELS * 2);
    buffer->ulBufferLength = DART_FRAMES * PCM_CHANNELS * 2;
    buffer->ulFlags = 0;
    queued_frames += DART_FRAMES;
}

static LONG APIENTRY MixEvent(ULONG status, PMCI_MIX_BUFFER buffer, ULONG flags)
{
    if (!running || !(flags & MIX_WRITE_COMPLETE))
        return 0;
    completed_frames += DART_FRAMES;
    if (status && !(flags & MIX_STREAM_ERROR))
        return 0;
    FillBuffer(buffer);
    if (running)
        mixer.pmixWrite(mixer.ulMixHandle, buffer, 1);
    return 0;
}

qboolean SNDDMA_Init(void)
{
    MCI_AMP_OPEN_PARMS open;
    ULONG rc;
    int i, rate = 22050;
    i = COM_CheckParm("-sndspeed");
    if (i && i + 1 < com_argc)
        rate = atoi(com_argv[i + 1]);
    if (rate < 8000 || rate > 48000)
        rate = 22050;
    memset(&open, 0, sizeof open);
    open.pszDeviceType = (PSZ)MCI_DEVTYPE_AUDIO_AMPMIX;
    rc = mciSendCommand(0, MCI_OPEN, MCI_WAIT | MCI_OPEN_TYPE_ID | MCI_OPEN_SHAREABLE, &open, 0);
    if (rc)
    {
        Con_Printf("DART open failed: %lu\n", rc);
        return false;
    }
    device = open.usDeviceID;
    memset(&mixer, 0, sizeof mixer);
    mixer.ulBitsPerSample = PCM_BITS;
    mixer.ulFormatTag = 1;
    mixer.ulSamplesPerSec = rate;
    mixer.ulChannels = PCM_CHANNELS;
    mixer.ulFormatMode = MCI_PLAY;
    mixer.ulDeviceType = MCI_DEVTYPE_WAVEFORM_AUDIO;
    mixer.pmixEvent = MixEvent;
    rc = mciSendCommand(device, MCI_MIXSETUP, MCI_WAIT | MCI_MIXSETUP_INIT, &mixer, 0);
    if (rc)
    {
        Con_Printf("DART setup failed: %lu\n", rc);
        SNDDMA_Shutdown();
        return false;
    }
    memset(&allocation, 0, sizeof allocation);
    memset(buffers, 0, sizeof buffers);
    allocation.ulNumBuffers = DART_BUFFERS;
    allocation.ulBufferSize = DART_FRAMES * PCM_CHANNELS * 2;
    allocation.pBufList = buffers;
    rc = mciSendCommand(device, MCI_BUFFER, MCI_WAIT | MCI_ALLOCATE_MEMORY, &allocation, 0);
    if (rc)
    {
        Con_Printf("DART allocation failed: %lu\n", rc);
        SNDDMA_Shutdown();
        return false;
    }
    allocated = 1;
    if (allocation.ulNumBuffers < DART_BUFFERS ||
        allocation.ulBufferSize < DART_FRAMES * PCM_CHANNELS * 2)
    {
        Con_Printf("DART returned buffers smaller than requested\n");
        SNDDMA_Shutdown();
        return false;
    }
    memset(&dma, 0, sizeof dma);
    dma.buffer = calloc(DMA_FRAMES, PCM_CHANNELS * 2);
    if (!dma.buffer)
    {
        SNDDMA_Shutdown();
        return false;
    }
    dma.channels = PCM_CHANNELS;
    dma.samples = DMA_FRAMES * PCM_CHANNELS;
    dma.samplebits = PCM_BITS;
    dma.speed = rate;
    dma.submission_chunk = 1;
    dma.gamealive = dma.soundalive = true;
    shm = &dma;
    completed_frames = queued_frames = 0;
    started = 0;
    running = 1;
    Con_Printf("OS/2 DART: %d Hz, 16-bit stereo\n", rate);
    return true;
}

int SNDDMA_GetDMAPos(void)
{
    dma.samplepos = (completed_frames & (DMA_FRAMES - 1)) * PCM_CHANNELS;
    return dma.samplepos;
}

void SNDDMA_Submit(void)
{
    int i;
    if (!running || started)
        return;
    started = 1;
    for (i = 0; i < DART_BUFFERS; ++i)
        FillBuffer(&buffers[i]);
    if (mixer.pmixWrite(mixer.ulMixHandle, buffers, DART_BUFFERS))
        Con_Printf("DART could not start PCM playback\n");
}

void SNDDMA_Shutdown(void)
{
    MCI_GENERIC_PARMS generic;
    running = 0;
    memset(&generic, 0, sizeof generic);
    if (device)
    {
        mciSendCommand(device, MCI_STOP, MCI_WAIT, &generic, 0);
        if (allocated)
            mciSendCommand(device, MCI_BUFFER, MCI_WAIT | MCI_DEALLOCATE_MEMORY, &allocation, 0);
        mciSendCommand(device, MCI_CLOSE, MCI_WAIT, &generic, 0);
    }
    device = 0;
    allocated = started = 0;
    free(dma.buffer);
    dma.buffer = NULL;
    dma.gamealive = dma.soundalive = false;
}
