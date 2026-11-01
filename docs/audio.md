<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Audio and MIDI

[Sound overview and recommended external sources](sound.md).

The MMPM bridge plays queued DART PCM and Standard MIDI Files through the shared
host audio stream. MIDI playback uses sampled instruments by default: all 128
General MIDI melodic programs, channel 10 percussion, program changes, pitch
bend, channel volume, expression and sustain. Tempo changes and track events
share one ordered playback timeline. Stop/pause/replay and completion notification
use the MCI device lifetime.

The host audio stream currently mixes mono PCM. TinySoundFont does not implement
all SoundFont modulators or chorus/reverb effects, so this is sampled General MIDI
playback rather than an exact emulation of a particular hardware synthesizer.

Builds pin the MIT-licensed [TinySoundFont](https://github.com/schellingb/TinySoundFont)
header at commit `853a0a171759f1ddba0de1442133a75912bbeffa`, and
[GeneralUser GS 2.0.3](https://github.com/mrbumpy409/GeneralUser-GS) by S. Christian
Collins at commit `684543d5e5efaef08d02be50dcda8d552478fa60`. CMake verifies SHA-256
hashes before use and copies the bank and both upstream licenses into
`build/soundfonts/`. The GeneralUser GS license permits use in software projects;
its complete license accompanies the bank.

Move `soundfonts/` with the worker when distributing a build. `TWOINE_SOUNDFONT`
can select another SF2 bank at runtime; the bank must contain all 128 bank-0
programs and the standard percussion bank. A missing/incomplete bank produces an
MCI device-not-ready result. It does not silently substitute tones.

For offline configuration, set `TWOINE_MIDI_DATA_DIR` to a directory containing
the four pinned files listed in `cmake/Midi.cmake`: `tsf.h`,
`TinySoundFont-LICENSE`, `GeneralUser-GS.sf2` and `GeneralUser-GS-LICENSE.txt`.
An explicit `-DTWOINE_MIDI_SYNTH=OFF` build retains a basic oscillator fallback
and prints that selection at configure and MIDI-open time. This profile does
not provide General MIDI instrument playback.

`midi_instruments` verifies rendered audio for all programs, distinct instruments
and percussion, expression/volume mute, pitch bend and sustain release. The DART
and unchanged-application integration checks remain separate from synthesis.

[External VOGONS topic discovery and downloads](vogons-soundfonts.md)
complement the pinned author catalogue.

## Bank selection

[SoundFont fetching and selection](soundfonts.md) uses a pinned download
catalogue and supports local SF2 files. `TWOINE_SOUNDFONT` takes precedence
over the selected bank in audio.conf; `TWOINE_AUDIO_CONFIG` selects that file,
otherwise it is `$XDG_CONFIG_HOME/2ine/audio.conf` or `~/.config/2ine/audio.conf`.
Absent a selection, the staged GeneralUser GS bank is used.

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
