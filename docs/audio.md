<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Audio and MIDI

[Sound overview and recommended external sources](sound.md).

The MMPM bridge plays queued DART PCM and Standard MIDI Files through the shared
host audio stream. MIDI playback uses sampled instruments by default: all 128
General MIDI melodic programs, channel 10 percussion, program changes, pitch
bend, channel volume, expression and sustain. Tempo changes and track events
share one ordered playback timeline. Stop/pause/replay and completion notification
use the MCI device lifetime.

The synthesizer renders stereo PCM through the shared MMPM host mixer. Extended builds use FluidSynth 2.4.8 for SF2/SF3/DLS and liquidsfz 0.4.2
for SFZ, with libInstPatch 1.1.7 supplying DLS conversion. The default bank is
FluidR3 GM 3.1 by Frank Wen, with his MIT license and recording/contributor
credits. Source archives, the bank and notices are pinned by SHA-256.

[Sound banks and channel mapping](sound-banks.md) describes native GUI import,
per-ball libraries, ZIP/SFZ folder installation, GM/GM2/GS/XG/MT-32 channel-layout
presets and individual overrides. It also describes format-specific behavior;
these presets do not supply proprietary Roland/Yamaha hardware emulation.

Keep `soundfonts/` beside a relocated worker. Outside a ball,
`TWOINE_SOUNDFONT` takes precedence over `soundfont` in `TWOINE_AUDIO_CONFIG` or
the per-user `$XDG_CONFIG_HOME/2ine/audio.conf` / `~/.config/2ine/audio.conf`.
Ball launch preparation supplies its own audio configuration and selected bank.

Offline builds use `TWOINE_MIDI_DATA_DIR` for the six files declared in
`cmake/Midi.cmake` and `TWOINE_FLUIDSYNTH_SOURCE_DIR`,
`TWOINE_INSTPATCH_SOURCE_DIR` and `TWOINE_LIQUIDSFZ_SOURCE_DIR` for the corresponding
source releases. Linux builds need GLib and libsndfile development packages;
native i386 builds need their matching i386 ABI and a multilib C++ compiler.
macOS builds use the matching GLib/libsndfile installation.

`TWOINE_MIDI_EXTENDED=OFF` retains TinySoundFont (MIT) and SF2-only playback.
`TWOINE_MIDI_SYNTH=OFF` retains the basic oscillator profile and prints that
selection; it does not provide sampled General MIDI instrument playback.

`midi_instruments` renders all 128 GM programs and percussion and verifies pan,
volume, expression, pitch bend and sustain. `midi_bank_formats` generates original
SF2/SF3/DLS/SFZ banks and checks real finite PCM, five profiles, routing and mute.
The DART and unchanged-application integration checks remain separate.

[All project sources, versions, authors and downloads](project-sources.md)
includes synthesis dependencies and assets. [SoundFont downloading](soundfonts.md)
and [VOGONS discovery](vogons-soundfonts.md) cover optional user-selected banks.

## Optional native MT-32

Configure `TWOINE_MT32_SYNTH=ON` to build Munt from the pinned source, or supply
`TWOINE_MT32_SOURCE_DIR` offline. Set `TWOINE_MIDI_BACKEND=mt32` and provide
`TWOINE_MT32_CONTROL_ROM` plus `TWOINE_MT32_PCM_ROM`. Equivalent configuration
keys are `midi_backend=mt32`, `mt32_control_rom` and `mt32_pcm_rom`.
No ROM is bundled or fetched. Missing or invalid ROMs produce a clear failure.

This backend receives native MT-32 program numbers and full SysEx messages.
It does not pretend that MT-32 is General MIDI; select `gm` and a GM/GS bank
for Doom's GM tracks. Split Standard MIDI File SysEx messages are reassembled
and delivered when their final fragment arrives. Framed messages in F7 escape
events are supported; raw non-SysEx escape data is rejected.

`midi_mt32_availability` covers enabled/disabled and missing/invalid ROM paths.
`twoine_midi_mt32_tests --rom-smoke` additionally renders actual notes when
separately supplied valid ROMs are available. A build or missing-ROM test alone
is not an MT-32 audio verification.
