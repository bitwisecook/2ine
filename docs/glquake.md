# GLQuake for OS/2

This port builds id Software's GPL GLQuake as a native OS/2 LX Presentation
Manager application. The same executable uses IBM OpenGL on Warp 4.52 and
2ine's Mesa/PGL implementation. The platform layer calls OS/2 APIs only.

The current port provides a fixed-size window, single-player maps, save/load,
keyboard controls, relative mouse movement, mouse buttons and Quake's TGA
screenshot command. Sound, CD audio and LAN networking are disabled. Local
client/server communication uses Quake's loopback driver.

## Run

Put `glquake.exe` beside an `id1` directory containing your Quake `pak0.pak`
and, for the registered game, `pak1.pak`. Game data is separate from the GPL
engine. The [SDL Quake shareware archive](https://www.libsdl.org/projects/quake/data/quakesw-1.0.6.zip)
contains a usable `id1/pak0.pak`.

On Warp, install the optional IBM OpenGL runtime from your OS/2 media.
`OPENGL.DLL` and its matching `RASTER.DLL` must be on LIBPATH or in the
application directory. Use a color desktop; the verified VM uses Cirrus VGA
at 1024x768. Start with a small window for IBM's software renderer:

```bat
glquake -width 320 -height 240 +map e1m1
```

On 2ine, build with `-DTWOINE_OPENGL=ON` and use the default `warp` OpenGL
ordinal profile. Run from the game directory so that `id1` is found:

```sh
/path/to/2ine/lx_loader /path/to/glquake.exe -width 640 -height 480 +map e1m1
```

Linux uses Mesa. macOS uses the KosmicKrisp/Metal configuration described in
[OpenGL](opengl.md); this port adds no platform-specific host graphics calls.
The macOS host runtime still needs separate macOS testing.

Use the original Quake keyboard bindings or change them in the Options menu.
Arrow keys move and turn; holding Alt strafes. Mouse motion turns/moves;
`+mlook` enables mouse look. Escape opens/closes the menu. The console
`screenshot` command writes `id1/quakeNN.tga`. Quake asks for confirmation
when quitting an active game.

Additional switches:

- `-nomouse`: leave the desktop pointer unconfined and disable relative motion.
- `-gl10`: force the stock OpenGL 1.0 texture compatibility path.
- `-inputlog`: record translated PM key/button/motion events in `glquake.log`.
- `-frames N`: stop after N calls to `Host_Frame`, including calls filtered by
  Quake's frame limiter. This is a bounded-run control, not a rendered FPS count.

`glquake.log` records startup, renderer selection, engine messages and shutdown.
Quake writes bindings/settings to `id1/config.cfg` on normal shutdown.

## Build on Linux

Obtain the [original id Software source](https://github.com/id-Software/Quake)
at revision `bf4ac424ce754894ac8f1dae6a3981954bc9852d`, Linux-hosted Open
Watcom 1.9 and the OS/2 Toolkit 4.5. The builder verifies the engine source
hashes before making a staged copy. It preserves the upstream GPL notices.

```sh
python3 tools/build_glquake.py \
  --source /path/to/Quake \
  --watcom /path/to/watcom \
  --sdk /path/to/os2tk45 \
  --output /path/to/glquake-build
```

To run the OS/2-hosted compiler and linker directly through 2ine's CLI, use
Open Watcom 2 and pass the loader. This mode uses 2ine's default C: mapping
of the host filesystem root:

```sh
TWOINE_BACKEND=native TWOINE_LIBPATH=/path/to/2ine-build \
LD_LIBRARY_PATH=/path/to/2ine-build \
python3 tools/build_glquake.py \
  --source /path/to/Quake --watcom /path/to/watcom2 \
  --sdk /path/to/os2tk45 --output /path/to/glquake-build \
  --os2-runner /path/to/2ine-build/lx_loader
```

The runner mode loads Watcom's OS/2 compiler and linker DLLs, compiles all
engine units, and links an OS/2 LX executable. `--backend unicorn` is a
runtime test-harness option; `TWOINE_BACKEND` selects the builder's CPU worker.

The output contains `glquake.exe`, per-unit compiler logs, `build.json` with
commands/input hashes and `source/` with the complete prepared source. The
original source, SDK and compiler installations are unchanged. Without `--os2-runner`, this helper runs the Linux i386 compiler and requires
host i386 execution support.

The build uses C versions of Quake's low-level routines, 32-bit enums for the
on-disk model records, and a 2 MB OS/2 stack for the player-skin scratch buffer.
It links the Toolkit's original Warp import library. Watcom's newer
`lib386/os2/opengl.lib` uses a different ordinal ABI and must not replace it.

## Build inside Warp 4.52

Use the prepared `source/` directory or the complete source archive accompanying
the executable. Install OS/2-hosted Open Watcom 2 and the OS/2 Toolkit.
Copy `%SDK%\lib\opengl.lib` to the source directory as `warpgl.lib` if it is
not already present. Run from a writable source directory:

```bat
set WATCOM=C:\WATCOM
set SDK=C:\OS2TK45
build.cmd
```

`build.cmd` sets the compiler header/DLL/library paths, compiles every unit,
links `glquake.exe`, and writes `build.rc` with the build result. The generated
`makefile` and `native.lnk` also support `wmake -h` with the same configured
Watcom environment. The batch recipe uses conservative compiler optimization settings and does not
require a make process. The archived OS/2-hosted Watcom 1.9 compiler DLL
crashed during repeated engine builds; the native build uses Watcom 2.

## Texture compatibility

IBM's retail renderer reports OpenGL 1.0.0. GLQuake requires texture objects and
subimage updates, so the port caches each texture's mip levels and sampler
parameters, then restores them using GL 1.0 display lists. A subimage update
updates the cached pixels and uploads the changed level.

A provider reporting GL 1.1 with named `glBindTexture` and `glTexSubImage2D`
exports uses its native texture objects. This is the Mesa path under 2ine.
The stock Warp path requires neither GLU nor an OpenGL Gold update.

## Verification

The owning integration harness runs the actual OS/2 executable with shareware
E1M1 under X11, waits for Quake protocol sign-on, captures the map/menu/moved
views, sends real keyboard events, checks Quake's screenshot file and confirms
normal exit. Both CPU workers and the forced GL 1.0 texture path use this test:

```sh
xvfb-run -a python3 tests/integration/glquake.py \
  --build /path/to/2ine-build --backend native \
  --program /path/to/glquake.exe --data /path/to/id1/pak0.pak \
  --work /path/to/isolated-test
```

Use `--backend unicorn` for the portable worker, `--gl10` for texture fallback,
and `--mouse` to exercise relative motion and firing.
Supply a new work directory for each run. Stock Warp verification uses a
private VM disk, IBM's original OpenGL runtime, the same executable, actual
PM input, an engine screenshot and the startup/shutdown log.
