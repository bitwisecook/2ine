/* Public domain. No rights reserved. Wholly AI-authored. */
/* MMPM's sequencer device: plays a standard MIDI file through the synthesizer
   or, in builds without one, the oscillator. */
#ifndef TWOINE_MMPM_MIMC_H
#define TWOINE_MMPM_MIMC_H
#include "os2/mm/mmpm/mci/mdm/mdm.h"

/* With lx_mci_monitor held: loads the MIDI file held by an open HMMIO. */
ULONG lx_mimc_open(LxMciDevice *device, ULONG hmmio);
ULONG lx_mimc_play(LxMciDevice *device, ULONG flags, unsigned char *parameters, USHORT user);
/* With lx_mci_monitor held: renders the next count frames of the sequence. */
void lx_mimc_render(LxMciDevice *device, float *samples, int count, int frequency);
/* With lx_mci_monitor held: releases the sequence and the synthesizer. */
void lx_mimc_close(LxMciDevice *device);
#endif
