# Shared C ball engine

This library owns the cross-platform ball format and behavior. macOS SwiftUI
calls its C ABI directly. The CLI and GTK adapters use the same library via
ctypes. No Python interpreter or script is shipped in the macOS application.

Build independently of the OS/2 runtime and UI dependencies:

```sh
cmake -S ball -B build-ball-engine
cmake --build build-ball-engine --config Release
```

Outputs: `twoine_ball` shared library, `libtwoine_ball_core.a` static archive
(`twoine_ball_core.lib` on MSVC), and the `2ine-ball` native JSON helper.
The public header is `include/twoine_ball.h`. ABI version 1 has no C structs
crossing language boundaries. Inputs/results are UTF-8 JSON; errors and results
are owned allocations that the caller frees with `tb_free`. `tb_request` is
synchronous and reentrant. Run it on a worker actor/thread, serialize changes
to the same ball, and never use it from the UI event loop for hashing/copying.

Request actions:

| Action | Input | Result |
| --- | --- | --- |
| `discover` | none | Default ball directories |
| `snapshot` | `ball` | `manifest`, canonical `roots`, effective `workingDirectories`, guest `environmentDefaults`, opaque `applicationRecords`, Latin-1 `configSys` text |
| `command` | `ball`, `arguments`, optional `build` | Updated snapshot |
| `runtime-build` | `ball`, `build` | Snapshot with selected runtime |
| `save-config-sys` | `ball`, `expected`, `text` | Snapshot; rejects a conflicting disk edit |
| `prepare-launch` | `ball`, `build`, `arguments` | Structured command, cwd, environment changes, receipt seed |
| `record-install` | `ball`, `receipt`, `programHost`, `exitCode` | Snapshot after recording the actual result |
| `host-path` / `guest-path` | `ball`, `path` | Canonical host path / mapped guest path |
| `browse` | `ball`, `path` | Drive-letter directory and entries (`name`, guest `path`, `hostPath`, `directory`) |
| `import-files` | `ball`, destination guest `path`, host `sources` array | Updated directory after a non-replacing recursive copy |
| `set-working-directory` | `ball`, absolute guest `path` | Snapshot with that drive's saved default |
| `save-application` | `ball`, `name`, `program`, `arguments`, optional `originalName`, `workingDirectories`, `environment`, `iconSource` | Snapshot after one validated manifest write |
| `guest-environment` | `ball`, optional app `environment` | Expanded effective guest variables |
| `application-icon` | `ball`, app `name` or guest `program` | `source` (`embedded`, `custom`, `none`), optional base64 `rgba` with dimensions or native image bytes in `image` |
| `image-preview` | Host image `path` | Native image data for frontend preview |
| `parse-arguments` / `format-arguments` | `text` / `arguments` | Structured argv / losslessly quoted display text |

Commands are arrays such as `["init"]`, `["map", "D", "/media"]`,
`["config", "env.LANG", "en_US"]`, `["unset", "system.trace_events"]`,
`["add-app", "--cwd", "C:\\TEMP", "App", "C:\\OS2\\APP.EXE", "a b", ""]`,
and `["remove-app", "App"]`. Launch arrays begin with `run`, `install` or `launch`.
The frontend owns asynchronous process monitoring, native logs/windows and UI
lifecycle. The C engine returns `unsetEnvironment` and `environment`; apply both
to the inherited host environment before spawning. Send the actual exit code
to `record-install`; negative values represent POSIX signals. No shell is used.
For Windows file pickers, `guest-path` explicitly interprets its input as a host
path, avoiding ambiguity between a host C: path and the guest C: drive.

The helper reads one request from stdin and prints its JSON response. With
`--execute`, a `prepare-launch` request instead starts the guest with the plan,
streams its output and returns its exit status, recording an installer receipt
when appropriate. POSIX uses fork/exec/wait; Win32 uses Unicode CreateProcess
with explicit CRT-compatible argument quoting. Native frontends can handle
process presentation themselves while reusing the C launch plan.

`platform.c` provides POSIX and Win32 file adapters. Windows paths use wide
filesystem APIs. Library/CLI builds are covered by the Linux/macOS/Windows CI
matrix; the full OS/2 runtime's supported hosts remain separately documented.

Vendored dependencies:

- cJSON v1.7.19, commit `c859b25da02955fef659d658b8f324b5cde87be3`, MIT.
  Local changes make its error storage thread-local and retain parsed number
  lexemes so unrelated 64-bit manifest metadata survives edits exactly.
- Brad Conte's public-domain SHA-256 implementation, commit
  `cfbde48414baacf51fc7c74f275190881f037d32`. Used for runtime identity and
  installer receipts, verified against Python hashlib in the engine tests.

## Per-drive and per-application state

The optional ball `workingDirectories` object maps letters to absolute OS/2
paths, for example `{"C":"C:\\DOOM","D":"D:\\INSTALL"}`. Unset entries default
to their drive root. `currentDrive` is used for unqualified paths. `D:SETUP.EXE`
uses D's own default; `D:\\SETUP.EXE` starts at D's root. Canonical paths cannot
escape the drive they name, including nested or duplicate mappings.

Saved applications may carry `workingDirectories` and `environment` overrides.
The program's drive selects the initial active directory; explicit legacy
`cwd`/`--cwd` still selects the process working directory. Every other drive
retains its own inherited or overridden directory. The core emits
`TWOINE_DRIVE_CWD_A` through `TWOINE_DRIVE_CWD_Z` for mapped drives and
`TWOINE_CURRENT_DRIVE`; the runtime seeds its per-drive state and passes current
values to guest subprocesses. Launch preparation never changes ball defaults.

Guest environment precedence is built-in defaults, CONFIG.SYS, ball overrides,
then application overrides. In overrides `%NAME%` is case insensitive and reads
the inherited layer or another override. A self-reference uses the inherited
value, so `PATH=%PATH%;C:\\DOOM` appends a directory. Cycles are rejected; undefined
references expand to an empty string. OS/2 path lists use semicolons. Guest PATH
is exported as OS2PATH, preserving the host PATH. Reserved host/control
variables cannot be overridden. Override evaluation is independent of JSON key
order. Empty strings are explicit values; absent entries inherit.

`save-application` validates all fields before changing the manifest and retains
unknown application metadata. `iconSource` imports a host image into a
content-addressed `icons/` asset; an empty string restores automatic extraction.
Omitting the field retains the existing choice. Default icons are decoded in C
from LX or OS/2 NE RT_POINTER resources, including bitmap arrays, monochrome
and color AND/XOR images, and iterated/EXEPACK2 LX pages. The desktop and PM use
the same bitmap decoder. Unsupported/missing embedded artwork returns `none`.
The frontend creates its native image object from the returned bytes. No guest
code runs to extract icons. Image assets stay in the ball when a launcher is
removed. The snapshot's opaque `applicationRecords` and `restore-app` command
allow Undo without frontend reserialization or loss of unknown fields.

Imports preflight the complete batch, reject conflicts and symbolic links,
and copy without deleting sources or replacing existing destinations. An I/O
failure may leave already copied items; the diagnostic states that explicitly.
Frontends own native drag/drop, pickers, image presentation, OS configuration
and asynchronous operation presentation. The core owns these data/path rules.

Format references: [IBM LX format](https://komh.github.io/os2books/os2tk45/lxref.htm),
[Open Watcom NE structures](https://github.com/open-watcom/open-watcom-v2/blob/master/bld/watcom/h/exeos2.h).
