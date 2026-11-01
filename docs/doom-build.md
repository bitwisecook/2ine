<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# OS/2 Doom source build

`tools/build_doom.py` builds a separate OS/2 LX executable from the published
[Doom 1.10 source archive](https://www.os2site.com/sw/games/action/doom/os2doom110-src.zip).
It requires that exact ZIP (SHA-256
`b4de031a2316d529f504d21f2095768aafa88eb789a7c46415f43186e53d15b9`), an OS/2
Toolkit 4.5 tree containing `h/` and `lib/`, and the Open Watcom 1.9 C/C++ tree
containing `binl/`, `h/`, and `lib386/`. Run on Linux with native i386 execution
support and the `patch` utility installed.

```sh
python3 tools/build_doom.py \
  --source-zip /path/to/os2doom110-src.zip \
  --sdk /path/to/os2tk45-master \
  --watcom /path/to/open-watcom-1.9/c-cpp \
  --output /path/to/doom-build
```

The output contains `os2doom.exe`, patched source, compiler/linker logs and a
`build.json` receipt with input/output hashes. Supply a Doom IWAD separately.
The original archived executable is never read or modified. Third-party source
and its original license remain in the output tree.

The patch replaces the source's minute-relative wall clock with monotonic
`DosQuerySysInfo(QSV_MS_COUNT)` time. A short process critical section serializes
UI and game callers; unsigned differences extend the 32-bit millisecond wrap.
The existing source configuration defaults to windowed DIVE output.
The desktop patch fits the game into a centered 4:3 viewport when resized,
clears the letterbox bars, and recenters relative mouse input in client
coordinates. `TWOINE_KEEP_ASPECT=0` selects the original stretched presentation.

Saves allocate their declared archive capacity independently of the screen
buffers, preserving the save-file layout on levels with larger state. Audio
shutdown stops the completion mixer synchronously before releasing its work
buffer, with serialized ownership and safe repeated closes.

Network startup initializes the requested level directly, retaining the player
and node state negotiated by Doom. The port's 32-bit command fields use 32-bit
network conversions, and complete command records retain their padding bytes
so the original packet checksum remains valid.

Build adaptations select the TCP/IP 4 headers expected by the source, make the
MUS converter definition agree with its declaration, and give the assembler
renderer explicit data types and literal external symbol names. The builder
normalizes DOS end-of-file markers and include-path separators in scratch
copies, preserving the supplied SDK tree. It links the authentic SDK imports
and matching Watcom runtime libraries.

`tests/tools/test_doom_build.py` compiles the actual clock patch body against
controlled time samples and concurrent callers. It also executes the patched
shutdown and save routines with delayed callbacks, concurrent close callers,
large archives and allocation canaries. The real GUI verifier is
`tests/integration/doom.py`; use `--source-built` to identify this variant and
`--seconds 70` to exercise gameplay beyond a minute.
`tests/integration/doom_network.py` runs two local peers with genuine UDP,
checks rendered movement and both players' recorded commands, and verifies
network departure and normal process exits.

The original source displays its build date and time. `--build-epoch` declares
those strings as reproducible metadata; it defaults to `SOURCE_DATE_EPOCH`, or
zero when unset. The default January 1, 1970 stamp is synthetic build metadata,
not a claim about the original release date. Use the same epoch and compiler/SDK
inputs to reproduce the complete executable bytes.
