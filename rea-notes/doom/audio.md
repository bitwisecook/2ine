# Doom 1.10 multimedia compatibility

This is 2ine host-tool implementation work, not original-source reconstruction.
API and layout authority is the retained OS/2 Toolkit 4.5 headers
`sdk/os2tk45-master/h/mcios2.h` (MCI commands, DART structures, prototypes),
`mmioos2.h` (MMIOINFO and memory-file flags), `meerror.h` (MCI errors), and
`tools/src/2ine-hal/tools/data/os2imports.json` (MDM ordinals 1/2/3,
MMIO ordinals 27/32). The port's published `source110/audio.c` establishes
its MCI/DART buffer lifecycle; the selected shipped executable remains unchanged.

MDM now supports amp-mixer open; 8-bit unsigned/16-bit signed, mono/stereo PCM
DART mix setup; guest-visible buffer allocation; asynchronous write completion;
volume, stop and close. Submitted samples are retained as converted host data
until consumed by the existing shared SDL audio generator. A separate delivery
thread invokes guest completion procedures with a thread TIB. Emulated
callbacks use a fresh private guest stack when the host thread has no active
invocation; nested callbacks retain the previous behavior.

MMIO supports read-only memory elements and copies bytes at open. Closing the
caller element does not invalidate an MCI sequencer's retained event data.
The sequencer parses standard MIDI tracks, variable-length delta times,
running status, tempo changes and note on/off. Basic sine-wave polyphony
produces audible music; it is **not General MIDI instrument emulation**.
Channel programs, sustain, pitch-bend, expression and instrument/percussion
banks are not implemented. MCI string/CD-audio commands return device unavailable.
These limits prevent a claim of complete MMPM/2 compatibility.

Regression source is `tests/os2libs/test_mdm.c` in the 2ine checkout.
The native command compiled with GCC i386 and linked against the current native
libmdm/libmmio/lib2ine plus libm. Running with SDL_AUDIO_DRIVER=disk passed:

- invalid/stale device IDs reject;
- two allocated stereo PCM buffers are consumed and each completion arrives;
- MMIO input is retained after the caller overwrites its source;
- the copied MIDI track opens, starts, stops and closes;
- stale MMIO handles reject.

SDL disk output was `sdlaudio.raw` in the workspace (the supplied
SDL_DISKAUDIOFILE variable was ignored by SDL3). It contained 294912 bytes of
F32LE stereo output, extrema +/-0.4882507324 and 56906 nonzero samples.
This proves actual backend sample consumption, not audible physical-device output.

The same MMPM test also passes against the portable libraries: it emits a real
guest completion procedure, calls the generated guest pmixWrite bridge and
checks the asynchronous guest counter. No target-specific synthetic callback
is substituted. The portable command uses SDL_AUDIO_DRIVER=dummy.

Both native and Unicorn execution contracts pass with the added asynchronous
four-word callback test. Commands:

```
env PATH=/usr/bin:/bin cmake --build tools/src/2ine-hal/build-sdl3-portable --target twoine_execution_tests --parallel 6
tools/src/2ine-hal/build-sdl3-portable/tests/twoine_execution_tests unicorn
env PATH=/usr/bin:/bin cmake --build tools/src/2ine-hal/build-sdl3-native --target twoine_native_execution_tests --parallel 6
tools/src/2ine-hal/build-sdl3-native/tests/twoine_native_execution_tests native
```

Actual game audio, input and exit require the parent gameplay verifier. The
published `audio_close()` source appears to free its prebuffer twice; this is
an original-port observation, not authorization to patch the game executable.

Native TIB follow-up: sound-enabled game startup exposed Linux i386 GDT TLS
slot exhaustion when a guest thread created another multimedia thread. Linux
inherits all three TLS descriptors into children. `lxSetOs2Tib` now reserves
one GDT index on its first attach and reuses that per-task descriptor in
all descendants, leaving glibc GS alone. Six nested host-thread generations
pass TIB attachment/detachment contracts on native and Unicorn. The actual
sound-enabled game probe subsequently stayed alive; parent input verification
remains the authority for gameplay completion.

Parser/resource review added malformed-header, missing-track, status-in-data
and truncated-track rejection tests; queue writes are bounded to 64 pending
packets and rejected after stop. MCI format setup accepts playback only.

A separate generated-LX regression, `tests/integration/file_compat.py`, now
checks the actual native and Unicorn guest bridges for DosQueryPathInfo and
DosQueryFileInfo. Both runs pass capacities 21/22/23/24/30, canaries, file size
and packed attribute fields. An output located at precisely the final 22 bytes
of the guest data object succeeds; Unicorn rejects a 22-byte output beginning
one byte later and other invalid pointers. Missing files, bad handles and
unsupported levels preserve the caller buffer. This specifically covers the
portable bridge's former unconditional 24-byte FILESTATUS3 requirement.
