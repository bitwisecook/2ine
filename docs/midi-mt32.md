<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Optional Roland MT-32 synthesis

The optional backend uses [Munt libmt32emu 2.8.3](https://github.com/munt/munt/tree/libmt32emu_2_8_3/mt32emu)
for native MT-32, CM-32L and LAPC-I MIDI. It preserves those devices' instrument
numbers, channel assignments and SysEx messages. It does not translate General
MIDI programs into MT-32 patches. Keep the default GeneralUser GS bank for
Doom's General MIDI score; select MT-32 explicitly for suitable MIDI material.

Build with a C++ compiler and the optional source dependency:

```sh
cmake -S . -B build-mt32 -DTWOINE_MT32_SYNTH=ON
cmake --build build-mt32 --parallel
```

Native Linux i386 additionally requires matching 32-bit C++ development files
(for example, `g++-multilib`). The default build leaves Munt disabled. Enabling
it fetches the official archive with a pinned SHA-256. Offline builds can use
`-DTWOINE_MT32_SOURCE_DIR=/path/to/munt-libmt32emu_2_8_3`, containing `mt32emu/`.

Supply a matching control/PCM ROM pair and select the backend:

```sh
export TWOINE_MIDI_BACKEND=mt32
export TWOINE_MT32_CONTROL_ROM=/path/to/MT32_CONTROL.ROM
export TWOINE_MT32_PCM_ROM=/path/to/MT32_PCM.ROM
```

Equivalent keys in the per-user audio configuration are `midi_backend=mt32`,
`mt32_control_rom=/path/to/MT32_CONTROL.ROM` and
`mt32_pcm_rom=/path/to/MT32_PCM.ROM`. Explicit environment paths take precedence.
No ROM data is bundled or downloaded. Munt identifies supplied full ROM images
by their content and rejects missing, unknown, wrongly typed or mismatched pairs.
Split ROM-chip dumps must first be combined into a supported full image.

The backend renders at its creation sample rate using Munt's internal converter.
The current multimedia mixer consumes a mono mix of its stereo output. Reset
reopens the synthesizer with the same ROMs and clears sounding notes/state.

The default MT-32 contract checks enabled/disabled builds and missing/invalid
ROM errors. The separate `twoine_midi_mt32_tests --rom-smoke` invocation requires
real supplied ROMs and checks actual finite, nonzero note samples. A successful
negative contract does not establish real-ROM audio playback.
