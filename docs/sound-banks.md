<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Sound banks and MIDI channel mapping

In either native app, select a ball, open Settings, then choose **Sound Banks
and Channel Mapping**. Import a bank, select it as the default or for an
individual channel, choose a mapping preset and save. Changes apply to the next
guest launch. Each ball keeps its own library and configuration.

## Bundled bank and provenance

The default is **FluidR3 GM 3.1**, the original stereo Release 3 by Frank Wen.
The unchanged bank is pinned to SHA-256
`74594e8f4250680adf590507a306655a299935343583256f3b722c48a1bc1cb0`.
Frank's [published recording/contributor account](https://github.com/pianobooster/fluid-soundfont/blob/v3.1/README)
describes his own/collaborators' recordings and edited public-domain samples.
His [MIT license](https://github.com/pianobooster/fluid-soundfont/blob/v3.1/COPYING),
full credits and original bank accompany the runtime. This records the author's
published provenance; it is not an independent rights audit of every sample.

GeneralUser GS is an optional user download, rather than a bundled bank. Its
author's sample-origin uncertainty remains documented in its own license.
See [all project sources, versions, authors and downloads](project-sources.md).

## Supported imports

| Format | Playback | Installation |
|---|---|---|
| SF2 | FluidSynth 2.4.8 | Copies the bank into the selected ball |
| SF3 | FluidSynth with libsndfile/Ogg Vorbis | Copies the compressed SoundFont |
| DLS | FluidSynth with libInstPatch 1.1.7 | Copies the DLS bank |
| SFZ | liquidsfz 0.4.2 | Copies the entire instrument folder, retaining samples, includes and notices |
| ZIP containing one bank | Same decoder as its bank | Extracts and retains the complete folder, including samples and notices |

For SFZ, choose an instrument in its own folder with its associated samples.
An archive containing multiple banks reports that ambiguity; extract it and
select the desired instrument. Links, traversal members, special files, more
than 10000 files/folders, excessive nesting and imports over 2 GiB are rejected.
Original files are kept. Importing a changed instrument folder creates a new
content identity; existing installed versions remain available.

SFZ definitions use their own sample/key/controller mapping. Bank/program/type
selection applies to SF2, SF3 and DLS; channel routing and mute also apply to SFZ.
Imports retain their existing licenses. Installing a file does not grant rights
to redistribute it in an exported ball.

## Mapping presets and overrides

| Preset | Defaults and bank selection |
|---|---|
| General MIDI | Identity channel routing, channel 10 percussion, melodic bank 0 |
| General MIDI 2 | GM channel layout; MSB 120 selects percussion, LSB selects a variation |
| Roland GS | GM channel layout; MSB selects a variation |
| Yamaha XG | GM channel layout; LSB selects a variation, MSB 126/127 selects percussion |
| Roland MT-32 channel layout | Inputs 2–9 route to outputs 1–8; input 10 remains percussion; unused inputs are muted |

Selecting a profile changes its bank interpretation. **Apply Mapping Defaults**
also resets the draft's 16 channel routes and overrides. Cancel discards the
draft. Every input channel can select an output channel, installed bank, fixed
program, physical bank number, melodic/percussion type and mute state.

Program **0** in the GUI follows MIDI program changes. GUI programs **1–128**
correspond to stored/MIDI program numbers **0–127**. Bank **−1** follows the
profile's bank messages; a nonnegative value selects a physical bank in the
SoundFont. Missing variations fall back to the same GM instrument, then the
default program where available. Multiple inputs routed to one output share
that output's FluidSynth MIDI state.

GS/XG presets do not emulate a Roland/Yamaha device or supply proprietary
instruments. The MT-32 layout does not translate MT-32 programs or implement LA
synthesis. [Optional Munt](midi-mt32.md) is the separate authentic MT-32 backend
and requires user-supplied ROMs.

## Shared engine and runtime interface

Both GUIs use `audio-snapshot`, `import-soundfont` and `save-audio` in the shared
C engine. Banks live under `soundfonts/` inside the ball. `ball.json` records
relative paths, hashes, the selected profile and all 16 channel overrides.
Unknown settings/channel fields survive edits by older clients.

Launch preparation writes `audio.conf` and supplies `TWOINE_AUDIO_CONFIG` and
`TWOINE_SOUNDFONT` to the worker. The file contains `soundfont`, `midi_profile`,
`channel_N=output,program,bank,percussion,muted` and `channel_N_soundfont` keys.
Output channels are 1–16; program/bank/type −1 means inherit. The GUI uses the
same configuration on Linux and macOS. A missing or unplayable selected bank
fails MIDI device creation instead of substituting a different bank.

Build dependencies and offline source/data overrides are declared in
`cmake/Midi.cmake` and `cmake/MidiBanks.cmake`. Extended playback is enabled by
default. `TWOINE_MIDI_EXTENDED=OFF` selects the smaller TinySoundFont SF2-only
fallback. `TWOINE_MIDI_SYNTH=OFF` selects the basic oscillator profile.
