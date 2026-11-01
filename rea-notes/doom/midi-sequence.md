# SMF SysEx retention and scheduling

Scope: 2ine MDM's sequencer parser, shared by SoundFont and optional Munt
backends. This change does not supply or qualify MT-32 ROMs.

The MIDI Association's [SMF specification landing page](https://midi.org/standard-midi-files)
identifies the authority. A retained public copy of *Standard MIDI Files 1.0*
(page 7) is available in this [USPTO exhibit](https://ptacts.uspto.gov/ptacts/public-informations/petitions/1459216/download-documents?artifactId=8hVQXHkK4zsyLcSUDNBOQjl9twvK68kUxffojKwQ6ut1ZvTe5bVcJa8).
It defines F0 payloads, F7 continuation/escape events and cancellation of running
status by SysEx/meta events. Munt's framing is corroborated by its
[upstream Synth implementation](https://github.com/munt/munt/blob/master/mt32emu/src/Synth.cpp).

The private `os2libs/midi_sequence.h` interface exposes immutable scheduled
events to MDM and its owning tests. Each SysEx event owns an exact copy of a
complete wire message, including F0 and terminal F7. The parser retains stable
same-tick order, track/tick coordinates, program/controller/voice events and
tempo changes. MDM dispatches the full message through `lx_midi_synth_sysex`.
MDM's synth branches are enabled by either TWOINE_MIDI_SYNTH or TWOINE_MT32_SYNTH,
so an explicitly configured Munt backend also works with SoundFont support off.

F0 events lacking an F7 terminator accumulate subsequent F7 continuation packets.
The assembled message is scheduled at the final continuation's tick. Its bytes
are preserved exactly; **fragment transmission timing is not reproduced**.
Meta events, including tempo changes, may occur between fragments. A channel
message or a new F0 before completion is rejected. Incomplete messages at track
end are rejected; continuation state never crosses a track boundary.

A standalone F7 escape may contain one or more complete framed SysEx messages;
each is scheduled in its original order at that event's tick. Other raw escape
contents, including System Common/Real Time bytes or incomplete framed messages,
are rejected rather than silently discarded. Interleaved real-time status bytes
inside a SysEx payload are likewise unsupported. Rejection reaches MCI open as
MCIERR_INVALID_BUFFER (5050).

Bounds: 1 MiB per framed message, 32 MiB aggregate retained SysEx payload and
262144 retained MIDI events. Partial payloads and earlier completed events are
freed on every parsing failure. Sequence close, failed synth creation and normal
MCI close also release all owned payloads. A cleared sequence has no event array
or retained count; reuse requires clearing an existing sequence first.

Verification (both passed):

```
env PATH=/usr/bin:/bin cmake --build tools/src/2ine-hal/build-sdl3-native --target twoine_mdm_tests --parallel 6
SDL_AUDIO_DRIVER=dummy tools/src/2ine-hal/build-sdl3-native/tests/twoine_mdm_tests
env PATH=/usr/bin:/bin cmake --build tools/src/2ine-hal/build-sdl3-portable --target twoine_mdm_tests --parallel 6
SDL_AUDIO_DRIVER=dummy tools/src/2ine-hal/build-sdl3-portable/tests/twoine_mdm_tests
```

`tests/os2libs/test_mdm.c` verifies exact assembled/escaped bytes after the input
media has been overwritten and freed; stable order among program/tempo/voice and
multiple SysEx events; completion-time scheduling across a tempo change; twelve
malformed/truncated/running-status cases; oversized payload rejection; cleanup
when a valid message precedes malformed data; and the existing dynamic MCI PCM,
MMIO ownership and sequencer lifecycle contracts. The direct parser checks need
no ROMs and do not claim hardware patch-memory effects. Native/portable build
logs are `.state/2ine/doom/sysex-build-{native,portable}.log`.

C/H formatting and `git diff --check` pass. Actual Munt audio remains conditional
on the separate backend/ROM verification recorded by its owner.
