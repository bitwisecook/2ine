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
| `snapshot` | `ball` | `manifest`, canonical `roots`, Latin-1 `configSys` text |
| `command` | `ball`, `arguments`, optional `build` | Updated snapshot |
| `runtime-build` | `ball`, `build` | Snapshot with selected runtime |
| `save-config-sys` | `ball`, `expected`, `text` | Snapshot; rejects a conflicting disk edit |
| `prepare-launch` | `ball`, `build`, `arguments` | Structured command, cwd, environment changes, receipt seed |
| `record-install` | `ball`, `receipt`, `programHost`, `exitCode` | Snapshot after recording the actual result |
| `host-path` / `guest-path` | `ball`, `path` | Canonical host path / mapped guest path |

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
