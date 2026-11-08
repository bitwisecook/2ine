/* Public domain. No rights reserved. Wholly AI-authored. */
#ifndef TWOINE_MIDI_BANKS_H
#define TWOINE_MIDI_BANKS_H
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct LxMidiBanks LxMidiBanks;
    LxMidiBanks *lx_midi_banks_create(const char *default_bank, unsigned rate);
    void lx_midi_banks_destroy(LxMidiBanks *banks);
    int lx_midi_banks_reset(LxMidiBanks *banks);
    void lx_midi_banks_event(LxMidiBanks *banks, unsigned status, unsigned key, unsigned value);
    void lx_midi_banks_sysex(LxMidiBanks *banks, const unsigned char *message, size_t length);
    void lx_midi_banks_render(LxMidiBanks *banks, float *samples, unsigned count, unsigned rate,
                              float volume, int channels);
#ifdef __cplusplus
}
#endif
#endif
