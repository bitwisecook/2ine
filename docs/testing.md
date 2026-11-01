<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Tests

Run runtime and execution contracts with `ctest --test-dir build --output-on-failure`.
They cover mixed-mode entry, callbacks, threads/TLS, memory bounds, resource
lifetimes, x87 state and host synchronization.

`legacy_file_apis` builds synthetic NE/LX guests without an external compiler.
It verifies writable preloaded NE stacks, NLS case mapping and export ordinals,
packed `DosFindFirst2` directory records, `DosOpen2` with empty extended
attributes, file read/close, nested critical/must-complete calls, portable
output-pointer validation, and DLL termination during failed process loading.
Nonempty extended attributes and higher `DosFindFirst2` information levels
remain unsupported and return errors.

The `host_platform` test checks argument packing (including empty arguments),
executable/process discovery, memory protection, clocks and UTC calendar
conversion. Linux additionally runs `host_mutex_owner` for robust mutex
owner-death recovery. These complement shared monitor timeout/cancellation tests.

`host_media` exercises SDL3 dummy audio consumption, software-rendered pixels,
presentation and translated events. It also checks that video shutdown leaves
audio running. `host_socket` transfers data through an IPv4 loopback connection
and checks readiness. Socket tests require local network access. Native execution
tests require the kernel's LDT service; restrictive sandboxes may deny it.

On Linux, `host_fullscreen` requires Xvfb and the X11/XTest shared libraries.
It checks Alt+Enter, startup fullscreen, scaled pixels, mouse coordinates and
restored placement using an isolated EWMH manager. `multimedia_stereo_disk`
captures SDL PCM and verifies independent channels and device volume.

Guest test source is in `tests/programs/`. Retained upstream guest binaries are
in `tests/programs/fixtures/`, with hashes and known provenance in the manifest.
They are guest fixtures, not host runtime dependencies.

## Developer tools

Supply the historical tools yourself. Copy `tests/integration/devtools-fixtures.example.json`
and set these paths:

| Key | Contents |
|---|---|
| `msc` | Microsoft C 6.0 `bin`, `include`, `lib` |
| `msasm` | MASM 5.10A `bin`, `lib`, protected-mode LINK/runtime |
| `ml6` | OS/2 `ML.EXE`, with adjacent `ML.ERR` |
| `msc386` | DDK CL386, compiler helpers and LINK386 |
| `ibm_bin`, `ibm_lib` | ICC/ILINK 3.6.5 EXEs/DLLs and matching LIB directory |
| `watcom` | Watcom 11.0c `binp`, `binp/dll`, `binw/wlsystem.lnk`, `lib386/os2` |
| `os2386` | OS/2 import library; recipes use Open Watcom 1.9 |

```sh
python3 tests/integration/devtools.py --build build --backend unicorn \
  --fixtures /path/fixtures.json --work /path/new-results
```

Use `--backend native` with the native Linux build. Each run needs a fresh work
directory. It retains command logs, objects, executables and `results.json`.

| Toolchain | Exercises |
|---|---|
| Microsoft C 6.0 | Compile/link/run NE, produce DOS MZ, directory handle reuse |
| MASM 5.10A + LINK | Assemble/link/run NE |
| MASM 6 | Small/large sources, diagnostics, assemble/link/run NE |
| DDK CL386 + ML6 + LINK386 | Compile/assemble/link/run LX, returning 42 |
| IBM ICC/ILINK 3.6.5 | Compiler helpers, linker thread, compile/link/run LX |
| Watcom 11.0c | Compile/assemble/archive/link/run LX and invalid-source diagnostics |

OMF checks accept omitted zero checksums and validate nonzero checksums. LX
checks validate object/page extents and entry/stack objects. The DOS MZ output
is inspected, not executed. No proprietary compiler fixtures are distributed.

The native Linux profile runs all six pipelines. Portable Microsoft C, MASM
and DDK pipelines run; the IBM ICC and Watcom WLIB fixtures currently terminate
with unmapped-memory faults under Unicorn. Those failures also occur on the
pre-cleanup build. Passing execution contracts do not imply complete compiler
or Presentation Manager compatibility. SDL's dummy/software tests do not
qualify a physical display or sound device.

The `presentation_manager` contract covers frame/client layout, window-registry
growth, retained presentation spaces, filtered queue insertion and timers. Native
contracts also test all four OS/2 stack-alignment offsets at a host API entry.
Optional archived GUI fixtures verify real pixels and X11 input; see
[GUI applications](gui.md) for exact targets, inputs and limits.

## Desktop service contracts

Application profiles register `application_settings` and `desktop_apis` with
CTest. The settings test runs separate writer/reader processes against temporary
profiles and registry files, validates merged and case-sensitive queries, binary
truncation, typed handle lifetime, write rollback and malformed native INI
records. The generated LX guest tests relocatable resource objects in EXEs,
resource references, counted strings, DOSCALL1 aliasing, settings bridges,
opaque window pointers and guest calls to the procedure returned by subclassing.
Portable guests additionally exercise invalid input/output spans. No Lotus
binaries are required for these tests.

`pm_window_apis` generates a separate LX guest that checks the actual WM_CREATE
callback's control pointer, CREATESTRUCT fields and guest strings, then exercises
window enumeration, presentation parameters, measured font records and text
extents through native/portable exports. HAL tests rasterize glyphs from all
12 pinned faces, inspect coverage, distinguish proportional/monospace advances
and verify style metadata. Host PM tests cover owner inheritance, callback-time
hierarchy queries, frame data, enumeration wrapping and queued-message retirement.

`pm_dialogs` verifies packed-template rejection, initialization and immediate
dismissal, owner restoration, nested modal processing, owner destruction,
dialog reuse, list state and WM_QUIT reposting. `pm_dialog_apis` builds an x86
LX fixture with a real dialog resource and guest WM_INITDLG callback; it checks
modal results, stock subclass procedure calls and invalid portable spans.
The media test verifies transparent, half-coverage and opaque glyph blending.

The optional `tests/integration/smartsuite.py` harness exercises Word Pro's real
new/edit/font/save/reopen/edit/save/reopen/exit cycle using separately supplied
installation files and settings. It records screenshots and document hashes;
see [GUI applications](gui.md) for invocation and limits. The native Linux
and Linux Unicorn document workflows are checked independently of the contract
suites. The harness allows longer interaction settling time for emulated
execution; it copies and redirects application preferences before startup.

PM contracts also cover logical-font text output, memory bitmaps and raster
copies, region clipping, presentation-space state, accelerators, list searching
and input-hook modification/removal. The generated legacy guest tests current
directory/path errors and an exit callback registered by a DLL that is later
released by its caller.

## Doom and multimedia

See [OS/2 Doom](doom.md) for the optional real-game input/save/load/audio/quit
check and separately supplied source/WAD fixtures. [Audio](audio.md) describes
DART and sampled MIDI synthesis. The service contracts require no game media.
