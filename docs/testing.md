<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Tests

Run runtime and execution contracts with `ctest --test-dir build --output-on-failure`.
They cover mixed-mode entry, callbacks, threads/TLS, memory bounds, resource
lifetimes, x87 state and host synchronization.

The `host_platform` test checks argument packing (including empty arguments),
executable/process discovery, memory protection, clocks and UTC calendar
conversion. Linux additionally runs `host_mutex_owner` for robust mutex
owner-death recovery. These complement shared monitor timeout/cancellation tests.

`host_media` exercises SDL3 dummy audio consumption, software-rendered pixels,
presentation and translated events. It also checks that video shutdown leaves
audio running. `host_socket` transfers data through an IPv4 loopback connection
and checks readiness. Socket tests require local network access. Native execution
tests require the kernel's LDT service; restrictive sandboxes may deny it.

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
