# Fatal failure reporting

Goal: in emulated mode a failure produces a clear dialog and a full report, never a
macOS crash report or Linux core file. Behaviour is documented in
[docs/runtime.md](../docs/runtime.md).

## Evidence

Measured 2026-11-06 on macOS 27.2 arm64, from a worktree of `99f5958`.

- A probe with handlers produced no `.ips` for SEGV, abort, trap, illegal instruction
  or stack overflow. The unhooked control produced one for each mode tried.
- Linux 6.8 aarch64, `core_pattern=core`, unlimited `RLIMIT_CORE`: the control wrote a
  core; handled SEGV, abort, trap and overflow exited 1 with none. This used the real
  `host_fatal.c`, Linux platform file and `test_fatal.c`, with trivial host stubs, not
  a full Linux runtime. x86-64 and i386 were compiled only.
- Approach from a clone of the Lotus ball exited 1 at `PMWINX:ordinal #636` and wrote
  its report to `<ball>/logs`, with no `.ips`. The report named the caller,
  `APPROACH object #1 +0x141FCC`.

On `01ed0db` the patch needed two private build fixes that are not part of it:
`doscalls/timers.c` in the source list and a `guest/arguments.h` include for
`pmshapi.h`. With them, 107 of 111 CTest tests passed, including `fatal_report`,
`missing_import_exit` and `source_boundaries`. All four failures come from `pmwp.c`
not compiling at that commit (`data_address_*` undeclared): `desktop_apis`,
`open32_apis` and `ball` cannot load PMWP, and `application_settings` needs a test
binary that links it.
Approach and the Swift suite were last run on `99f5958`.

## macOS dialog findings

SDL's message box appeared after 0.2 s, after 5.3 s or not at all within 15 s. A plain
`NSAlert` was created at layer 0 but stayed off screen: the active Space was
full-screen. The accessory policy, `MoveToActiveSpace | FullScreenAuxiliary` and the
modal panel level put it on screen in three runs of three on that Space.

## Not established

- Nobody has seen the rendered dialog: screen capture was unavailable. Window owner,
  bounds, level and on-screen state were measured.
- Keyboard focus. `lsappinfo` kept reporting another application, which may only
  reflect the accessory policy or cooperative activation.
- The Linux SDL dialog, the Linux `lx_loader` path and native i386 at runtime.
- The GNOME `Adw.AlertDialog` and the macOS alert in the Ball app. Only their message
  functions are tested.

## Known limits

- Threads not created by 2ine, such as SDL's, have no alternate stack. Overflowing
  one is not reported; `RLIMIT_CORE` limits file cores, but a macOS crash report or a
  piped Linux core handler can still run.
- Only the failing thread's stack is reported.
- Missing imports show module and ordinal. Names need the catalogue at runtime.
- An exit-phase failure shows no dialog, deliberately: earlier probes recorded a guest
  null read during shutdown. A fault before the program requests exit still shows one.
- `abort()` sites without `lx_host_fatal` report only SIGABRT and a backtrace; their
  own stderr message stays in the session log.
