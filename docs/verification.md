# Reproduce the developer-tool gate

`tests/devtools.py` requires every configured fixture and a new output directory.
Missing inputs fail the gate. It records input/source/runner/log/output SHA-256s,
commands, relevant environments, exits and assertions in `results.json`.
Historical compiler binaries and SDK libraries are not provided by this fork.

Copy `tests/devtools-fixtures.example.json` and set absolute fixture paths:

| Key | Required layout |
|---|---|
| `msc` | Microsoft C 6.0 tree: `bin`, `include`, `lib` |
| `msasm` | MASM 5.10A tree: `bin`, `lib`, including protected-mode linker/runtime |
| `ml6` | OS/2 `ML.EXE`; adjacent `ML.ERR` is required by this gate |
| `msc386` | Staged DDK `cl386.exe`, compiler stages/runtime and `link386.exe` |
| `ibm_bin` | ICC/ILINK 3.6.5 EXEs and runtime DLLs in one directory |
| `ibm_lib` | Matching IBM compiler `LIB` directory |
| `watcom` | Watcom 11.0c tree: `binp` plus `binp/dll`, `binw/wlsystem.lnk`, `lib386/os2` |
| `os2386` | OS/2 import library (the demonstrated configuration uses Open Watcom 1.9) |

```sh
python3 tests/devtools.py --build build-portable --backend unicorn \
  --fixtures /absolute/path/fixtures.json --work /absolute/path/fresh-results
```

Use `--build build-native --backend native` for the native lane. Normal execution
prints `All configured developer pipelines passed` only after every pipeline
passes. Each compiler runs under the selected loader; IBM's child stages launch
that same loader. Logs and output artifacts are retained for inspection.

| Pipeline | Required assertions |
|---|---|
| Microsoft C 6.0 | Compile/link/run NE; compile/link DOS MZ; legacy directory allocation beyond ten IDs, reuse and close/error behavior |
| MASM 5.10A + LINK | Assemble/link/run NE |
| MASM 6 | OMF validation, repeated large-source assembly, invalid-source diagnostic, assemble/link/run NE via `tests/test-ml6.sh` |
| DDK CL386 + ML6 + LINK386 | Compile/assemble/link valid LX without CRT and run it, returning 42 |
| IBM ICC/ILINK 3.6.5 | Child compiler stages, linker worker thread, valid OMF/LX and run with expected output |
| Watcom 11.0c | Compile, assemble, archive, link valid LX, run with expected output; invalid C fails with diagnostic and no object |

OMF validation accepts the specified zero/omitted checksum form, checks nonzero
checksums and records omitted counts. See [TIS OMF 1.1](https://openwatcom.org/ftp/devel/docs/omf.pdf).
LX validation checks header/object/page extents and entry/stack objects. It caught
an existing seek-to-end workaround that made LINK386 emit a corrupt 512-byte LX;
the corrected file API now produces an executable returning 42.

The native Linux demonstration runs a native i386 worker on a Debian x86-64
kernel with `CONFIG_X86_16BIT=y`. That Linux test machine was created in QEMU TCG
on ARM macOS. QEMU supplies the test machine; it is not an execution dependency
of the macOS Unicorn application. Intel/ARM Windows and other host CPU builds
have not been qualified.

Runtime contracts cover mixed protected-mode execution, contexts/x87, checked
memory, active borrows, unaligned atomics, TIB/TLS isolation, nested callbacks
across 16/32-bit entries, API argument conventions and managed thread exit.
Host tests cover typed-handle ownership/reuse, mutex ownership/timeouts,
monitor cancellation, finite mutex-wait cancellation, once initialization and
thread/operation cleanup. The same execution contract source runs on native
Linux and Unicorn. CI builds the portable runtime contracts on macOS and Linux
from pinned Unicorn source; it does not distribute or run the historical fixtures.

The recorded local verification also includes AddressSanitizer/UBSan runtime
contracts and AddressSanitizer application pipelines. These instrument 2ine's
host code; the installed Unicorn library and translated guest instructions are
not compiler-instrumented. Application leak detection is disabled because the
worker intentionally retains some guest allocations until process exit.

`verification.json` contains compact, source-bound results and fixture/output
hashes. Recorded publication-only workflow changes do not change compiled
application inputs; their tested/published hashes are listed separately. Full local `results.json` files additionally retain commands, environment
and log locations. The gate qualifies these configured pipelines and their
assertions, not every archived binary or an OS/2 reconstruction milestone.
