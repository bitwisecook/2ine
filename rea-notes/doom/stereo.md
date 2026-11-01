# Doom stereo host-audio path

This is 2ine host compatibility work, outside reconstructed OS/2 source gates.
The bounded gate is independent left/right PCM at the actual SDL output,
with native i386 and Unicorn guest callbacks, plus existing mono callers,
volume, singleton registration, retirement and shutdown contracts.

The previous runtime registered a mono float stream. MDM averaged the two
DART channels, and the SoundFont/Munt wrappers also rendered mono. SDL's
stereo output consequently duplicated the mono result. A stereo device
format by itself was not channel-separation evidence.

The HAL now supports mono and stereo frame callbacks through
`lx_host_audio_open_channels`; the existing open function retains mono
semantics. Runtime mixing uses interleaved stereo. Existing mono generators
retain their additive mono callback and are mixed into both channels; the
new stereo registration preserves each generated channel. Its loader-state
entry is appended, leaving prior field offsets unchanged.

MDM retains interleaved PCM pairs, duplicating input only for actual mono
DART formats. Rate conversion and volume operate on both channels. MIDI
uses TinySoundFont interleaved stereo or Munt's stereo output without a
mono fold. The old direct synth render functions remain mono wrappers.
The sine fallback remains a centered mono instrument by design.

Tests use distinct sine/cosine DART channels with 4:1 amplitude, 22050 Hz
source rate and 50% MCI volume. The disk-capture variant reads SDL F32LE
stereo output and asserts independent channels, approximately 16:1 energy,
0.244/0.061 peaks, and completion of both guest buffers. The ordinary media
suite also covers simultaneous mono/stereo registrations, singleton
rejection, callback retirement and repeated shutdown. MIDI coverage adds
extreme controller-10 pans and stereo zero-volume silence.

Fresh Release builds at `.state/2ine/doom-finish/build-native` (GCC 15.2,
i386 native ABI) and `build-portable` (Clang 20, x86-64 host with Unicorn
IA-32 guest ABI) pass all six audio tests:

```
env PATH=/usr/bin:/bin ctest --test-dir .state/2ine/doom-finish/build-native --output-on-failure -R 'host_media|multimedia|midi_|mt32'
env PATH=/usr/bin:/bin ctest --test-dir .state/2ine/doom-finish/build-portable --output-on-failure -R 'host_media|multimedia|midi_|mt32'
```

The native command requires host execution: sandboxed i386 programs receive
SIGSYS before the test body. Rerunning outside that syscall sandbox passes;
there is no native audio test failure. Both use MIDI=ON, MT32=OFF. Six tests
per backend pass: `host_media`, `multimedia`, `multimedia_stereo_disk`,
`midi_instruments`, `midi_preferences`, `midi_mt32_availability`.

Each actual SDL disk capture has 65,536 bytes / 8,192 stereo frames, 4,460
frames with unequal channels, energy L/R 132.5289368731901 /
8.298396955709904, and peaks 0.2441253662109375 / 0.06103515625. Both files
are identical, SHA-256
`53b65816c4f13ab39709e9130ad659d9259e4a34d97939ce7f96d3b3d9154ed1`.
The callbacks consume both 1,024-frame guest buffers. These measurements are
backend-consumption evidence; they do not establish physical-speaker output.

Repeating `midi_instruments` with
`TWOINE_AUDIO_CONFIG=/home/jimd/src/os2/.state/2ine/doom/audio.conf` passes
on both backends, covering all 128 sampled programs, percussion, old mono
controls, stereo pan and stereo volume using the user's uploaded SC-55
SoundFont (SHA-256 `ae2e56711dc40835d3f97d975d80eadb2abdc76dc9da8b055f7a3c18dab20974`).

Host-library identities at this audio gate:

| Artifact | SHA-256 |
| --- | --- |
| native lib2ine.so | d185db474f33b38143f98dea326c74e3e81d558a1ae707009d75c33cfbd241fd |
| native libmdm.so | 2406cc6e0f6ee3acbf78e3289170af55618aeda14769061e2aae42127824b16a |
| portable lib2ine.so | 22a06d94671e58d8eaf99ec4831b09b6fe9544265c538509059b57c4f81e8dd0 |
| portable libmdm.so | 17b8637f6a5b2bfe9238b524b5452d595e17cf720867dc020b918d85a7ff9dc3 |

Actual Doom stereo remains the parent's gameplay gate. Actual MT-32 rendering
remains unverified because no control/PCM ROM pair was supplied; this change
preserves its stereo C API path without asserting ROM audio.

## Source audio-shutdown lifetime correction

The parent's final native combat run with the network-capable source build
(SHA-256 `a31b8d68c6d7b7d8fbff6ccb007f96b3bfd3ece82f926ae8d5e0cfe6038be9cd`)
saved health 73 but crashed after `Exiting OS2Doom...` / `Writing defaults 44`.
The retained failure is `.state/2ine/doom-finish/probe-complete/worker.log`.
The read fault is unmapped `0x00600000`, EIP `0x0004e4f3`.

This is guest code, not the host loader: the ELF loader's text begins near
`0x0804a000`; the LX code object's relocation base is `0x00020000`.
The generated Watcom MAP and independently extracted LX code establish:

- `0x0004e4f3`: object 2 offset `0x2e4f3`, `__MemFree + 0x33`, instruction
  `mov (%edx),%edi`; EDX is the corrupted free-list link `0x00600000`.
- return `0x0004be65`: `free_` / `_nfree_` (`0x2be20`) caller chain.
- return `0x0002cd6e`: main's `for (i=4; i>=0; i--) free(screens[i]);` loop.

Thus the screen-free loop encounters an already corrupted guest heap. The
published source's `audio_close` frees `prebuffer` before synchronously
retiring the completion mixer, then frees the same pointer again. A mixer
completion uses global `prebuffer` in `process_mixer16s` and can overwrite
allocator links after the early free. The source additionally calls audio
shutdown from both `I_Quit` and `GameLoopThread`. These are concrete source
lifetime defects; no invalid-pointer acceptance or host fault suppression
was added. The exact corrupting write was not recorded with a watchpoint.

`research/patches/doom110-clock.patch` now also changes `audio_close` to claim
one shutdown owner under a short `DosEnterCritSec` section, release that
section before synchronous STOP (which joins the completion thread), then
deallocate guest buffers, close the device, and free/clear `prebuffer` once.
Concurrent callers return while the owner finishes. Failed STOP/deallocation
retains the buffer and releases ownership for retry. The published source
archive and shipped executable remain untouched; this is an explicit source
build correction.

The executable patch-body contract in `tests/tools/test_doom_build.py`
executes the actual hunk against a backend which touches the work buffer
until STOP returns. It verifies stop/deallocate/close/free ordering,
STOP/deallocation failure retries, close failure, repeated shutdown, and four
concurrent shutdown callers while the owner is blocked in STOP. Both clock
and shutdown tests pass with `/usr/bin/cc -pthread -Wall -Wextra -Werror`.
Ruff formatting/check and Git whitespace checks pass.

Two clean, independently extracted source builds with the same pinned SDK,
Open Watcom and epoch-zero inputs are byte-identical:

```
env PATH=/usr/bin:/bin python3 tools/src/2ine-hal/tools/build_doom.py --source-zip .state/2ine/doom/v110/source.zip --sdk sdk/os2tk45-master --watcom compilers/open-watcom-1.9/c-cpp --output .state/2ine/doom/audio-source-final
env PATH=/usr/bin:/bin python3 tools/src/2ine-hal/tools/build_doom.py --source-zip .state/2ine/doom/v110/source.zip --sdk sdk/os2tk45-master --watcom compilers/open-watcom-1.9/c-cpp --output .state/2ine/doom/audio-source-repro
```

Both `os2doom.exe` outputs have SHA-256
`58a9802aa055f67844b0c19e477239945510b907a5cfc5592f75c4c30f427b97`;
`build.json` in each directory records source, patch, tools, headers,
libraries and patched source hashes. The frozen host runtimes are unchanged.
The parent's final combat/quit gate must establish that this removes the
observed exit failure in the actual game.

## Enabled Munt stereo compile/link gate

Fresh outputs under `.state/2ine/doom-finish/munt-check` compile the current
`midi_mt32.c` and `midi_synth.c` with `TWOINE_MT32_SYNTH=1`,
`TWOINE_MIDI_SYNTH=0`, `MT32EMU_API_TYPE=1`, `-O3 -Wall -Wextra -Werror`,
using the retained generated Munt headers and matching static library.
The native build uses `-m32` and the previously retained i386 C++ runtime;
the portable host build is x86-64. Both link successfully. `commands.json`
records each direct compile/link invocation; `inputs.json` records exact
adapter/test/library hashes. The old probe directories were read only.

Both fresh `mt32_probe` executables exit 0 on the existing missing/invalid
control+PCM ROM tests; an added wrapper calls both stereo entrypoints with
NULL instances and verifies that asymmetric output markers remain unchanged.
Native execution uses the required i386 syscall-sandbox escape. This
establishes that the current stereo code compiles/links in the enabled
profile and retains negative contracts. It is not real-ROM or MT-32 audio
rendering evidence. The supplied ROM pair remains absent.

## Large savegame overflow: observed heap-corruption cause

The `58a980...` audio-lifetime correction did **not** remove the observed
combat-exit heap failure. Both final-native and final-portable logs still
fail inside `__MemFree` when freeing screens after combat save. Therefore
the audio defect above is an independently corrected source defect, not a
proven explanation of that particular corruption.

The parent identified `G_DoSaveGame` using
`save_p = savebuffer = screens[1] + 0x4000`, while `V_Init` allocates each
screen independently with exactly 320*200=64,000 bytes. Only 47,616 bytes
remain beyond that offset. E1M3's serialized world exceeds 81 KB, overwriting
adjacent guest heap storage, including the allocation metadata encountered
by the screen-free loop. Small E1M1 saves stayed within the available
screen tail and did not expose this defect. The identical logical failure
on native and Unicorn was consistent with a guest-source overflow.

The source patch now uses an independent
`Z_Malloc(SAVEGAMESIZE, PU_STATIC, NULL)` allocation of 0x2c000 bytes, writes
the original save format, frees it with `Z_Free`, and clears the global save
pointers before rebuilding the back screen. The zone allocator preserves
the original four-byte alignment used by archive padding. The original
archive writers, size limit, headers, payload and trailer are unchanged.
`G_BeginRecording_II` already uses independently allocated `demobuffer`;
no other screen-backed save alias was found.

The executable full-function regression runs the actual patched
`G_DoSaveGame` body with 85,429-byte and 180,221-byte archive payloads. It
checks the exact header fields, time, four-byte padding, archive bytes and
0x1d trailer, the allocation's requested size and guard bytes, write-before-
free ordering, repeated save/free, and unchanged bytes in all five 64-KB
screens. Together with clock and audio ownership checks, all three source
patch tests pass. Final Ruff format/check and Git whitespace checks pass.

Two further clean source builds are byte-identical:

- `.state/2ine/doom/save-source-final/os2doom.exe`
- `.state/2ine/doom/save-source-repro/os2doom.exe`

SHA-256 is
`7604e9aa38e91c862ea07cd25ec4aa7578a912b844c75ad7d62cb9907b532aee`.
They use the same exact build command above with the respective output
paths. Each `build.json` inventories all inputs. Current patch SHA-256 is
`f0d623b088d66945fc7fed840f5c8fc1c7570f94aab79470316c307eed54d3bc`;
`build_doom.py` SHA-256 is
`cda4a20d77f6ec250de6ff622e83d033e0b1739879ed8275e8d208347b876961`.
The parent owns actual native/portable combat-save-load-quit verification
for these outputs. Host runtimes remain unchanged by the save correction.
