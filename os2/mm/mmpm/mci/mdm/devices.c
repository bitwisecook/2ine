/* Public domain. No rights reserved. Wholly AI-authored. */
/* The waveaudio and sequencer device table, the host audio generator that
   mixes them and the per-device delivery thread. Callbacks run off the SDL
   thread. */
#include "os2/mm/mmpm/mci/mdm/mdm.h"
#include "os2/mm/mmpm/mci/admc/admc.h"
#include "os2/mm/mmpm/mci/mimc/mimc.h"
#include "emulator/audio_mixer.h"

enum
{
    /* Thread IDs of delivery threads, above any guest thread ID */
    DELIVERY_THREAD_ID_BASE = 0xff00,
    DELIVERY_STACK_SIZE = 65536,
    DELIVERY_POLL_MS = 100
};

LxMciDevice lx_mci_devices[MCI_DEVICE_COUNT];
LxHostMonitor *lx_mci_monitor;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static int audio_registered;

static void initialize(void)
{
    lx_mci_monitor = lx_host_monitor_create();
}

void lx_mci_devices_initialize(void)
{
    lx_host_once(&once, initialize);
}

LxMciDevice *lx_mci_find_device(ULONG handle)
{
    for (unsigned i = 0; i < MCI_DEVICE_COUNT; i++)
        if (lx_mci_devices[i].id == handle && handle)
            return &lx_mci_devices[i];
    return NULL;
}

static int generate(void *unused, float *samples, int count, int frequency)
{
    (void)unused;
    lx_host_monitor_lock(lx_mci_monitor);
    for (unsigned i = 0; i < MCI_DEVICE_COUNT; i++)
    {
        LxMciDevice *d = &lx_mci_devices[i];
        if (!d->id || d->stopped || d->paused)
            continue;
        if (d->kind == LX_MCI_SEQUENCER)
            lx_mimc_render(d, samples, count, frequency);
        else
            lx_admc_render(d, samples, count, frequency);
    }
    lx_host_monitor_unlock(lx_mci_monitor);
    return 1;
}

static void *delivery(void *argument)
{
    LxMciDevice *d = argument;
    uint8 tibspace[LXTIBSIZE] = {0};
    unsigned char stack_marker;
    GProcessState.initOs2Tib(tibspace, &stack_marker, DELIVERY_STACK_SIZE,
                             DELIVERY_THREAD_ID_BASE + d->id);
    uint16 selector = GProcessState.setOs2Tib(tibspace);
    lx_host_monitor_lock(lx_mci_monitor);
    while (!d->stopped)
    {
        if (lx_admc_deliver(d))
            continue;
        if (d->ending)
        {
            d->ending = 0;
            ULONG window = d->notify_window, user = d->notify_user;
            int notify = d->notify;
            lx_host_monitor_unlock(lx_mci_monitor);
            if (notify && window)
                lx_mci_notify(window, d->id, MCI_NOTIFY_SUCCESSFUL, user);
            lx_host_monitor_lock(lx_mci_monitor);
        }
        else
            lx_host_monitor_wait(lx_mci_monitor, DELIVERY_POLL_MS);
    }
    lx_host_monitor_unlock(lx_mci_monitor);
    GProcessState.deinitOs2Tib(selector);
    return NULL;
}

ULONG lx_mci_start_audio(LxMciDevice *d)
{
    if (!audio_registered)
    {
        if (!GProcessState.registerStereoAudioGenerator(generate, NULL, 1))
            return MCIERR_DEVICE_NOT_READY;
        audio_registered = 1;
    }
    if (!d->thread)
    {
        lx_host_monitor_lock(lx_mci_monitor);
        d->stopped = 0;
        lx_host_monitor_unlock(lx_mci_monitor);
        d->thread = lx_host_thread_create(delivery, d, NULL);
        if (!d->thread)
            return MCIERR_OUT_OF_MEMORY;
        __atomic_store_n(&GProcessState.ne_threads_started, 1, __ATOMIC_RELEASE);
    }
    return 0;
}

void lx_mci_devices_terminate(void)
{
    if (audio_registered)
    {
        lxUnregisterAudioGenerator(generate, NULL);
        audio_registered = 0;
    }
    if (lx_mci_monitor)
        for (unsigned i = 0; i < MCI_DEVICE_COUNT; i++)
            if (lx_mci_devices[i].id)
                lx_mci_stop(&lx_mci_devices[i], 1);
}
