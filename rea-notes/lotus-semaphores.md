<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Freelance semaphore and startup metrics

Freelance's reported `PMWINX:ordinal #96` is `CreateSemaphore`, confirmed by
IMPDEF records in the original `sdk/os2tk45-master/lib/pmwinx.lib` in the parent
workspace. The same SDK establishes `OpenSemaphore` = 507 and
`ReleaseSemaphore` = 562; `os2win.h` provides their parameter types. The original
PMWINX DLL's name tables also establish `GetDialogBaseUnits` = 270 and
`GetCurrentTime` = 260. Its SHA-256 is
`07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`.

The original Freelance executable at
`/Users/jimd/Documents/Lotus.2ineball/drive_c/LOTUSW4/FLG/FLG.EXE` has SHA-256
`4d5a3b5ac9df801c4ec584801b0f1b313b55e27d69dd5e7c65e1c1d0cfd30cb1`.
Probes use the existing isolated `build-lotus-exports-current/Probe.2ineball`;
the user installation and executable bytes are preserved.

## Behavior

Open32 now creates typed counting semaphore handles and supports release,
wait and close. Unnamed waits use the existing host monitor. Named semaphores
use the host's ball-scoped shared backing with atomic count updates, bounded
initialization, case-sensitive names and lifetime references. Named waits poll
at one millisecond, as existing named OS/2 events do. Opening/creating an
existing named semaphore retains its current count and maximum; handles have
separate access rights. Waits consume one permit. Overflow and invalid output
pointers leave the count unchanged. A guarded wait releases its resource borrow
on ordinary completion or supported host thread cancellation.

Windows ACL descriptors and inheritable handles are not supplied by the host
provider; requests for these fail explicitly. OpenSemaphore accepts the explicit
standard/semaphore access mask, without a Windows generic-rights/ACL provider.
The checked guest bridges validate names, SECURITY_ATTRIBUTES and optional
previous-count output spans before executing the operation.

The new handle kind is appended after existing kinds to preserve their values,
so the local update also
needs the rebuilt core handle registry in `lib2ine.dylib`. Updating only
`libpmwinx.so` would leave the old registry rejecting the new kind.

GetDialogBaseUnits measures the existing system font and returns packed
horizontal/vertical units. GetCurrentTime is the legacy alias of GetTickCount.
GetDeviceCaps supplies square-pixel aspect metrics for display and raster printer
contexts: axes 36, diagonal 51, a relative GDI aspect vector. These are renderer
capabilities, not values recovered from a specific original display driver.

API contracts were checked against Microsoft's primary documentation for
[ReleaseSemaphore](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-releasesemaphore),
[WaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject),
[GetDialogBaseUnits](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getdialogbaseunits)
and [GetDeviceCaps](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getdevicecaps).

## Checks and original-program evidence

The focused guest contract covers count/maximum validation, consumption,
timeouts, overflow, invalid/stale handles, pointer validation, named object
reopening/lifetime, access restrictions, worker wakeups, cancellation cleanup
and a real two-process named semaphore handoff. Open32's existing suite also
checks nonzero dialog units, system-font height and nonzero square-pixel aspect
metrics.

The isolated Release snapshot in `build-lotus-exports-current/source` and
`runtime-make` excludes concurrent uncommitted workspace changes. Before release
it was updated to committed base `71f6b23` (which preserves the network/TLS work)
plus this task's staged changes. Test command:

```
ctest --test-dir build-lotus-exports-current/runtime-make --output-on-failure \
  -R '^(open32_semaphores|open32_apis|legacy_file_apis|lx_forwarded_exports|desktop_apis|pm_dialog_apis)$'
```

The six focused suites pass; the updated-base TLS shim and network guest checks
also pass. One combined run timed out after the legacy harness printed all
checks as successful; its isolated rerun passed in under one second.
Unicorn tests run outside the macOS sandbox because the
sandbox traps ARM host-register reads before guest initialization. Logs and
probe commands are retained under `build-lotus-semaphore-current/`.

The original program calls `CreateSemaphore(NULL, 0, 1, NULL)` successfully,
then creates its preloader worker. It next reaches ordinals 270 and 260;
implementing those exposes a divide-by-zero at `001b:00028361`.
Unpacked object 1, offset `0x18361`, contains `idiv esi`. The earlier calls
at `0x28221` and `0x28231` request GetDeviceCaps indexes 40 and 42, storing
results at `0x2fbcc4` and `0x2fbcc8`; the latter becomes the zero divisor.
REA/Ghidra 12.2 independently shows this data flow in
`build-lotus-semaphore-current/freelance-divide-rea.json` (the original import
calls appear as unresolved fixup placeholders; the argument indexes and direct
data flow are explicit). The page bytes/disassembly are independently retained.

Correct aspect metrics remove that fault. Freelance then registers many more
classes, initializes DDE and reads custom resources before reporting
`MEM_BAD_POINTER` and an `Abort, Ignore?` console prompt. This is a remaining
original-program memory/resource compatibility failure, not successful GUI
startup. The prompt repeats on EOF in the headless probe; the first-failure
context is retained in `pixel-aspect/first-failure.log` rather than treating
continued process lifetime as success. No original bytes were patched and
no assertion was bypassed.

Local package, signature, installation and rollback records are retained under
`build-lotus-semaphore-current/release/` when this update is installed.
