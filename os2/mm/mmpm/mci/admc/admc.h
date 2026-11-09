/* Public domain. No rights reserved. Wholly AI-authored. */
/* MMPM's waveaudio device: DART (MCI_MIXSETUP, MCI_BUFFER and the mixer write entry). */
#ifndef TWOINE_MMPM_ADMC_H
#define TWOINE_MMPM_ADMC_H
#include "os2/mm/mmpm/mci/mdm/mdm.h"

ULONG lx_admc_mixsetup(LxMciDevice *device, ULONG flags, unsigned char *parameters);
ULONG lx_admc_buffer(LxMciDevice *device, ULONG flags, unsigned char *parameters);
/* The mixer write entry: queues count MCI_MIX_BUFFER records at address. */
LONG lx_admc_queue_buffers(ULONG handle, ULONG address, ULONG count);
/* With lx_mci_monitor held: mixes the device's queued PCM into samples. */
void lx_admc_render(LxMciDevice *device, float *samples, int count, int frequency);
/* With lx_mci_monitor held: completes one played buffer, releasing the monitor
   during the guest callback. Returns 0 when none is waiting. */
int lx_admc_deliver(LxMciDevice *device);
/* With lx_mci_monitor held: drops queued buffers; closing also unmaps the
   MCI_ALLOCATE_MEMORY buffers. */
void lx_admc_discard(LxMciDevice *device, int closing);
#if TWOINE_PORTABLE_LOADER
/* The guest-callable mixer write entry (bindings.h); 0 if it cannot be made. */
ULONG lx_admc_write_entry(void);
#endif
#endif
