# OS/2 Doom under 2ine

Tool compatibility work only; no reconstruction milestone gate changes.
Acceptance: unchanged OS/2 Doom 1.10 executable renders real animated gameplay,
accepts keyboard/mouse, produces actual audio samples, saves and reloads a game,
and quits normally. Native and portable results are recorded separately.

Selected binary/source from https://www.os2site.com/sw/games/action/doom/
(os2doom110.zip and os2doom110-src.zip). Shareware DOOM1.WAD from dmos219s.zip
on that same archive, not a commercial IWAD. Downloaded 2026-11-01.

- `.state/2ine/doom/downloads/os2doom110.zip`: `a047003cf5908322e26658c7f7cd9ea0f34b981d6cc642aff896ec79a1496ffc`
- `.state/2ine/doom/downloads/os2doom110-src.zip`: `b4de031a2316d529f504d21f2095768aafa88eb789a7c46415f43186e53d15b9`
- `.state/2ine/doom/v110/os2doom.exe`: `360993334c9f033339f674e2a248fd8b712aea6013cfcb430158768126ed9e2d`
- `.state/2ine/doom/v110/doom1.wad`: `1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`

Source evidence and ABI authority: published port source retained under
.state/2ine/doom/source110; sdk/os2tk45-master/h and real SDK samples, plus
per-import LX fixup ordinals. Native worker is i386 C ABI; imported _System
calls must retain observed stack conventions. Changes go in tools/src/2ine-hal,
which contains existing unrelated local PM work. Preserve that work.

Baseline: cwd .state/2ine/doom/v110, TRACE_NATIVE=1, TWOINE_LIBPATH set to
tools/src/2ine-hal/build-sdl3-native, worker lx_loader os2doom.exe.
Sandbox blocks native i386 setup; approved execution reports missing mdm.
The older dmos219s doom.exe instead fails to load its hardware ioseg dependency.
No game runtime gate passed. Runtime state/logs are .state/2ine/doom.

An isolated codex-doom auto-continue record is armed. This Codex session has no
loaded OpenCode plugin; this does not establish automatic continuation.

Next: implement DIVE video and MMPM audio, resolve import requirements, then
run real X11 gameplay/save/load/input checks. No DONE marker exists.

## Runtime progress

Unchanged SHA-bound v1.10 now renders animated 640x480 gameplay, accepts real
XTest keyboard input, saves and loads a named game, produces real F32 audio
and quits through F10/Y with exit0 on native and Unicorn. Scratch probe receipts
are diagnostic; final verifier remains pending. Dependencies DIVE, MDM/MMIO,
SO32DLL/TCP32DLL are now real implementations. Filesystem fixes reject empty
filename/directories and support the observed 22-byte FILESTATUS prefix safely.
PM fixes client activation/focus, visible regions, pointer control and cross-thread
posts; native TLS reserves one per-process descriptor index across descendants.

Long-run blocker is an original Doom bug: published i_system.c:I_GetTime computes
ticks from tp.seconds only, so minutes wrap backward. Actual archived binary
reaches its panic WinMessageBox call on rollover. Preserve original archived
bytes and correct DOSCALLS time semantics. A source rebuild with a monotonic
clock is a separate application artifact, not reconstructed OS/2 core source.
No patched binary or fake API time is used. Next: verify that source-built
artifact across a minute plus save/load/audio/input/quit on both workers.

Full native contracts20/20 and portable25/25 pass before later PM thread-post
contract addition. Multimedia has basic sine MIDI rather than General MIDI
instruments; CD audio/direct VMAN hardware fullscreen are not implemented.
Detailed subsystem evidence: [DIVE](dive.md), [audio](audio.md), [network](network.md).

## Final verified application profile

`2ine_verify.py --spec .state/2ine/doom/verify.json --marker .state/2ine/doom/DONE`
passed after two fresh concurrent real-X11 runs against frozen native and
portable runtime copies (`runtime-sc55-native`, `runtime-sc55-portable`).
Each ran for 70 seconds beyond a wall-clock minute, with 128-program sampled
MIDI enabled and the user's uploaded SC-55 bank selected through audio.conf.
The harness confirms that exact bank was opened and hashes it before/after play.

Source-built game SHA-256 is
`dbbe20fd3fc75f3a32fbfc91e36493dfc6abc33e5ed0bb419b482a0c5955e739`.
Its separately supplied published sources and patch/build recipe are documented
in [source-build.md](source-build.md). Original archived bytes remain unchanged.
User bank SHA-256 is
`ae2e56711dc40835d3f97d975d80eadb2abdc76dc9da8b055f7a3c18dab20974`
(47,324,042 bytes; 128 bank0 programs and 8 percussion presets). Metadata does
not establish recording origin or redistribution terms; this is a private
user-supplied asset, not a pinned public-catalogue release.

Native saved game times were 80 ->211 ->87 after loading the initial save;
portable times were 79 ->210 ->87. Both changed actual scene pixels under
keyboard and mouse, consumed pistol ammunition, produced millions of nonzero
F32 samples, kept rendering and quit through F10/Y with exit0. Final receipts
retain exact worker/library/target/WAD/bank hashes, input command, screenshots
and logs under `.state/2ine/doom/final-sc55-{native,portable}/`.
The auto-continue record is disabled after the verifier generated DONE.

Current complete local contracts pass native28/28 and portable33/33, including
other existing work in this shared checkout. All six native historical compiler
pipelines pass (`compiler-regressions/results.json`). The owning tools Python
suite also covers the actual clock patch body, parser and download behavior.
General MIDI instruments, preferences and SysEx are separate tested contracts;
Munt enabled native/portable isolated builds validate missing/invalid-ROM failure.
Actual MT-32 audio is unverified because the user has no control/PCM ROMs.

Persistent playable files are `.state/2ine/doom/playable`; `scripts/2ine-doom`
selects the verified runtime and saved SC-55 preference. `scripts/2ine-soundfonts`
wraps author-bank download/selection and `forum` topic discovery/download.
User-selected local bank data remains in scratch/cache, never product source.

Scope limits: verified windowed single-player profile, digital effects and MIDI.
VMAN direct hardware fullscreen, physical joystick and CD audio remain unavailable.
Two-peer Doom network gameplay and visible macOS desktop behavior were not run;
actual UDP and guest legacy socket contracts pass on both Linux workers.
No OS/2 reconstruction milestone gate changes.

## External SoundFont tooling

The source-fetching follow-up provides pinned author downloads, local imports,
VOGONS topic discovery and selected-file downloads. Actual user-requested topic
88648 was read with ordinary public HTTP/curl, yielding 5posts and21links.
Author zz_denis links SC55_zzdenis v0.5; a real selected download produced a
completeGM SF2, 298185428bytes, SHA
c8c2aad1fe4cc55a7cfdf66307cf2fbe7e9e5407a8c8d4e8101ab96444c3c00d.
It remains privatecache data and is not the user's uploaded47MB bank. No bank
redistribution license is inferred from forum post licenses.

User subsequently selected mt32-pi's Recommended SoundFonts wiki as the
preferred external-source index. docs/sound.md links both that wiki and the
mt32-pi repository; named source/download choices follow that published list.
No new sound bank is authored or included in product source.

Final external index implements19 named choices from all mt32-pi wiki groups.
Three have verified automatic download providers; other entries retain upstream
pages and explicit availability/format/purchase notes. Actual named YamahaXG
fetch produced56456276bytes, SHA
01ae3f8826b73b7acc757f8f90f0ee99e2770b01c4ba70be029361d1a6311ce0,
and completeGM SF2 coverage. The uploadedSC55 selection was preserved.
Owning Python suite now includes download/catalogue/forum/index regressions;
Ruff, formatting, ty, generatedbindings and diff checks pass. Doom-scoped C/H
formatcheck passes; fullcheckoutformatter still reports unrelated OpenGL work
being developed separately. These findings do not invalidate frozen runtime
identity receipts.


## Completed gameplay follow-up

[Full playability completion](finish.md) supersedes the earlier windowed-only
scope limits. Default native/Unicorn launchers, normal menu startup,70-second
play, doors/live combat, host fullscreen, actual stereo SC-55 PCM, persistent
saves, >81KB save/load, clean exits and genuine two-player UDP games all pass.
Reproducible source-built EXE SHA-256 is
`7604e9aa38e91c862ea07cd25ec4aa7578a912b844c75ad7d62cb9907b532aee`.
The source variant additionally repairs network startup/serialization, unsafe
audio shutdown and the screen-backed save-buffer overflow. Archived bytes
remain unchanged. Runtime profiles are frozen in runtime-full-native/portable;
`scripts/2ine-doom` selects them, retaining the uploaded SC-55 preference and
persistent game saves/configuration. Final evidence and verifier-created DONE
are in `.state/2ine/doom-finish/`. Current scoped host contracts pass25/25
native and30/30 portable (OpenGL disabled); owning Python tools pass40 tests.
Munt stereo adapters compile/link and missing/invalid-ROM checks pass both
ABIs. Actual MT-32 rendering still needs ROMs; physical joystick/CD, direct
VMAN hardware and visible macOS desktop remain outside the verified profile.
No core reconstruction milestone changes.
