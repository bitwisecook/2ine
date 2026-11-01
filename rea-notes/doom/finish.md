# Doom playability completion

This is an application compatibility gate for host tooling, separate from the
OS/2 core reconstruction milestones. It extends the earlier windowed E1M1
profile with normal menu startup, live monsters, doors, stereo, host fullscreen,
saves after process restart, larger level saves and two-player network games.

Inputs retain their previous authority: archived OS/2 Doom 1.10 LX executable
SHA-256 `360993334c9f033339f674e2a248fd8b712aea6013cfcb430158768126ed9e2d`,
published source ZIP SHA-256
`b4de031a2316d529f504d21f2095768aafa88eb789a7c46415f43186e53d15b9`,
shareware IWAD SHA-256
`1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`,
Open Watcom 1.9 and actual OS/2 Toolkit 4.5 imports. The separately rebuilt
application remains OS/2 LX. The original archived executable is unchanged.
Published source and actual import/calling-convention evidence guide these
application fixes; this is not a byte-identity reconstruction claim.

## Implemented behavior

- HAL/runtime/DART/MIDI retain independent left and right PCM samples, with a
  mono adapter for existing generators. Uploaded SC-55 remains selected through
  `.state/2ine/doom/audio.conf`. See [stereo](stereo.md).
- Alt+Enter enters desktop fullscreen while preserving guest logical dimensions,
  pixel canvas and pointer mappings. `TWOINE_FULLSCREEN=1` selects startup
  fullscreen. F11 remains a guest key. See [fullscreen](fullscreen.md).
- Both real peers exchange and record movement commands through UDP. The
  source variant corrects startup clearing of network state and the port's
  32-bit packet serialization/padding. Optional process-local IPv4 binding
  permits two peers on one host. See [network gameplay](network-game.md).
- Published `audio_close` freed its work buffer before stopping the callback
  and freed it again. The source variant synchronously stops before releasing
  storage, serializes close ownership and makes repeated close safe.
- Published `G_DoSaveGame` used `screens[1]+0x4000` as its archive buffer. At
  320x200 the independent screen allocation left only47,616 bytes. E1M3 with
  monsters produces >81KB, overwriting guest heap metadata. Both workers
  reproduced the resulting free-list failure. The source variant allocates
  the declared SAVEGAMESIZE independently and releases it after writing.
  The file layout is preserved; the larger archive is reloaded by the game.

The audio fix alone did not remove the combat-save failure: retained first
final attempts in `.state/2ine/doom-finish/final-{native,portable}` show the
remaining corruption with executable58a9802a... . The save-buffer alias is the
confirmed source of that remaining overwrite. None of these failures was
hidden by changing host OS/2 APIs or ignoring game exit status.

## Build and verification

Two clean source builds, `.state/2ine/doom/save-source-final` and
`save-source-repro`, produce byte-identical executable SHA-256
`7604e9aa38e91c862ea07cd25ec4aa7578a912b844c75ad7d62cb9907b532aee`.
Their build.json files bind source patch, compiler, SDK/imports, options,
synthetic epoch-zero timestamp and resulting artifacts. The source patch is
`tools/src/2ine-hal/research/patches/doom110-clock.patch` and the reproducible
entrypoint is `tools/src/2ine-hal/tools/build_doom.py`.

Host build commands and output are retained in
`.state/2ine/doom-finish/configure-{native,portable}.log`, CMakeCache.txt and
`build-{native,portable}.log`. Native uses GCC i386; portable uses Clang and
Unicorn. Both use the existing pinned SDL3, FreeType, Liberation and MIDI inputs;
OpenGL is disabled for this Doom-specific profile. Frozen launch workers are
`.state/2ine/doom/runtime-full-{native,portable}`; their runtime.json manifests
bind worker, libraries, dependent SDL/Unicorn binaries, fonts and bank inputs.
Their runtime source snapshots also record315 C/H/build-file hashes.

The actual launch entrypoint is `scripts/2ine-doom`, using the persistent
`.state/2ine/doom/playable` directory and the selected SC-55 config. This update
preserves that directory's saves and configuration. `TWOINE_BACKEND=unicorn`
selects the portable worker; Alt+Enter toggles fullscreen.

Acceptance command:

```
env PATH=/usr/bin:/bin python3 .agents/skills/os2-re-2ine/scripts/2ine_verify.py --spec .state/2ine/doom-finish/verify.json --marker .state/2ine/doom-finish/DONE --log .state/2ine/doom-finish/DONE.log
```

The spec invokes the retained verify-all.py: both default launcher paths,
normal menus,70-second live-game runs, genuine keyboard/mouse/fire, clock
rewind through saves, fresh-process reload, fullscreen/restore, independent
stereo capture, separate E1M3 combat/door use and >81KB save/load, normal quit,
and two actual UDP peers per backend. Result receipts bind exact worker,
libraries, executable, IWAD, bank, commands, environment and captured pixels.
A changed worker/library during a run fails acceptance. DONE is only written
by the verifier after the complete command and artifacts pass.

Final acceptance passed; the verifier created `.state/2ine/doom-finish/DONE`.
Full receipts are `.state/2ine/doom-finish/result.json`,
`verified-{native,portable}/result.json` and
`verified-network-{native,portable}/result.json`.
Both game runs lasted70.03 seconds; default launcher selection and menu startup
were verified against the actual `/proc/PID/exe`. Both combat saves were81,406
bytes, with player health73; both reloaded and resaved as81,086 bytes. Actual
main-game audio had3,487,661 native and3,532,656 portable unequal stereo frames.
Every game process exited0. Both network pairs recorded identical two-player
command streams, remote movement and normal departure. Runtime manifests and
worker/library identities remained unchanged throughout acceptance.
The isolated auto-continue record is disabled after this pass.

## Scope

The playable profile is Linux native and Unicorn with desktop fullscreen,
digital effects and sampled MIDI, using separately supplied game assets.
Direct VMAN hardware access, physical joystick and CD audio are unsupported.
Actual MT-32 audio remains unverified without the user's control/PCM ROMs;
current stereo-capable Munt adapters compile/link and reject missing/invalid
ROMs in both ABIs. Visible macOS desktop behavior was not tested. No original
OS/2 milestone gate changes, publication or deployment is claimed.

Short final-source probes passed on both workers in
`.state/2ine/doom-finish/save-probe-{native,portable}/result.json`:
normal default launcher/menu, fullscreen, stereo, three in-process saves,
fresh-process load, E1M3 combat,81,246-byte save and81,086-byte reload/resave,
and three normal process exits. Native saved player health58, portable61.
The owning Python suite passes40 tests, including three compiled source-patch
contracts. Full native CTest25/25 and portable30/30 pass. Ruff lint/format,
ty and git diff whitespace checks pass. These checks are independent of the
subsequent passing70-second/network acceptance command.
