<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# OS/2 Doom

Doom runs as an OS/2 LX application on both native Linux i386 and the portable
Unicorn worker. Its video uses DIVE, input uses PM, effects use DART and music
uses memory-backed MIDI with [sampled General MIDI instruments](audio.md). [Fetch and select banks](soundfonts.md) or import
a local SF2 with the same tool.

The archived Doom 1.10 executable has a game-clock bug: it uses only the
seconds field of `DosGetDateTime`, so its clock goes backward at each minute.
The published source also needs startup and packet fixes for multiplayer.
Larger saves need storage separate from the screen buffer, and audio shutdown
must stop callbacks before releasing its mixer storage.
Use the source build below for sustained play. It remains an OS/2 executable;
the original archived executable is neither required nor patched by the build.

## Build the application

Supply OS/2 Doom 1.10 source, OS/2 Toolkit 4.5 and Linux Open Watcom 1.9.
The [OS/2 Doom archive](https://www.os2site.com/sw/games/action/doom/) supplies
`os2doom110-src.zip`. The builder requires its SHA-256 to be
`b4de031a2316d529f504d21f2095768aafa88eb789a7c46415f43186e53d15b9`.
The `dmos219s.zip` archive contains the freely distributed shareware
`DOOM1.WAD`; commercial Doom/Doom II IWADs must be supplied separately.

From this repository:

```sh
python3 tools/build_doom.py --source-zip /path/os2doom110-src.zip \
  --sdk /path/os2tk45 --watcom /path/open-watcom-1.9/c-cpp \
  --output /path/doom-build
```

This builds the published C sources and original assembly renderer. The patch
uses the millisecond system counter with wrap handling and synchronization.
Save archives use independent storage, and shutdown stops audio callbacks
before releasing their work buffer.
Network fixes preserve the game state during startup and serialize the port's
32-bit tic commands with initialized padding. Other changes align the supplied source's MIDI prototype and assembly data
extern declarations with the compiler, and select the correct socket headers.
The [reproducible builder](doom-build.md) pins its displayed build timestamp
with `--build-epoch` (default `SOURCE_DATE_EPOCH`, or zero). This is synthetic
build metadata. It normalizes DOS filename/include separators and records inputs,
compiler options, commands and output hashes in its output directory.
Third-party source retains its original license; no game code or WAD is
included in this repository.

## Play

Keep the built `os2doom.exe`, a shareware `doom1.wad` and `default.cfg` in a game
directory. Use this initial windowed configuration:

```text
screenwidth 640
screenheight 480
fullscreen 0
use_mouse 1
use_joystick 0
key_up 17
key_down 31
key_strafeleft 30
key_straferight 32
key_left 99
key_right 100
key_fire 29
key_use 57
```

Run from that directory, setting the build directory to its absolute path:

```sh
TWOINE_LIBPATH=/path/2ine/build LD_LIBRARY_PATH=/path/2ine/build \
  /path/2ine/build/lx_loader ./os2doom.exe
```

W/S move, A/D strafe, arrow keys turn, Control fires and Space uses doors.
F2 saves, F3 loads, Escape opens the menu and F10/Y quits. Alt+Enter toggles
desktop fullscreen; `TWOINE_FULLSCREEN=1` starts there. Keep the guest
`fullscreen` configuration at zero to use DIVE. F11 changes gamma. The built-in console
opens with the backtick key. Keep the `soundfonts/` directory when relocating
a worker; `TWOINE_SOUNDFONT` selects another compatible complete SF2 bank.

The source build keeps a 4:3 game image when resized, centering it with black
bars instead of stretching. Set `TWOINE_KEEP_ASPECT=0` to use stretching instead.
Mouse turning uses the resized client area's center.
For explicit host mouse capture, set `TWOINE_MOUSE_CAPTURE=1` before launching.
The mouse starts released. On macOS, the title bar has **Capture Mouse** /
**Release Mouse** controls showing the **Control–Option** shortcut, matching
[UTM's capture/release shortcut](https://docs.getutm.app/basics/controls/).
Click the game or use the title-bar button to capture; Control–Option toggles
capture. Changing focus releases it and returning to the window leaves it
released. Guest pointer warps and cursor hiding are suppressed while released.
Set `TWOINE_MOUSE_CAPTURE=0` to disable capture and guest pointer warps entirely.
Leaving it unset retains the application's original pointer behaviour.
These environment options are independent launch settings that a ball manager
can store per ball; they do not change a global preference or Doom's config.

In the OS/2 reconstruction workspace, `scripts/2ine-doom` runs the prepared
persistent game directory. `TWOINE_BACKEND=unicorn scripts/2ine-doom` selects
the portable worker. `TWOINE_DOOM_HOME` and `TWOINE_BUILD` override the game and
worker directories. Your saves and configuration stay in the game directory.
[Two-player network games](doom-network.md) work on both workers.

## Verify

On Linux, the real application harness needs Xvfb, X11/XTest and the game files:

```sh
python3 tests/integration/doom.py --build /path/2ine/build --backend native \
  --program /path/doom-build/os2doom.exe --wad /path/DOOM1.WAD \
  --source-built --work /path/fresh-results --seconds 70
```

It records actual pixels, keyboard movement, mouse turning, ammunition use,
three named saves, a verified rewind of game time after loading, continuing
frames across a minute, nonzero SDL PCM output and a normal menu quit.
`--restart-load` verifies the save in a fresh process; `--require-stereo` checks
that left and right samples differ. `--fullscreen-check` exercises Alt+Enter
and restoration through a real X11 window manager protocol. `--combat-check`
starts a separate E1M3 encounter on skill 4, opens its initial door, requires
player damage or a monster kill, and reloads the larger save.
`--launcher /path/scripts/2ine-doom`
tests the workspace launcher with an isolated game directory. Combine
`--launcher-default-build --menu-start --monsters` to verify default worker
selection and start a normal game through its menus.
A worker or library change during execution fails the run. Use `--backend
unicorn` for the portable worker. `TWOINE_DOOM_EXE` and `TWOINE_DOOM_WAD` CMake
options register the same 70-second `doom_gameplay` CTest when supplied.

`dive_video`, `dive_guest_apis`, `pm_visible_regions`, `pm_thread_posts`,
`legacy_socket`, `multimedia`, `midi_instruments`, `file_compat` and
`guest_file_compat` cover the underlying services without game media.

Desktop fullscreen, single-player combat, stereo effects/music, saves across
process restarts and two-player UDP games are verified on Linux native and
Unicorn workers. Direct VMAN hardware access, physical joystick and CD audio
are unsupported. The sampled synthesizer supports melodic programs, percussion
and common controllers; it does not emulate a specific hardware MIDI module's
effects. Munt requires separately supplied MT-32 ROMs. Xvfb/disk-audio tests do
not qualify physical devices or visible macOS desktop behavior.
