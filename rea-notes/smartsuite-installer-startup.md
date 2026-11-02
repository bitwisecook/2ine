# SmartSuite installer startup probes

This bounded 2ine job runs the user's original ENGLISH/INSTALL.EXE and its
embedded unpack helper against extracted media, using isolated profiles and
temporary output. No installation choices or writes to the user's ball are
part of these probes.

## Inputs and isolation

Original media is extracted at `build-cmd-runtime-review/lotus`; D: maps there.
C: maps the separate `build-cmd-runtime-review` test drive with imported OS/2
support. Profile/registry paths and TEMP point under
`build-cmd-runtime-review/install-direct-state`. SDL dummy video/audio and the
software renderer permit message/dialog startup without accepting choices.
The probe captures bounded stdout/stderr (256 KiB prefix plus final 1 MiB when
needed), permits 20 seconds, then terminates the probe process group.

INSTALL.EXE resource type 300/name 5801 contains a 77,200-byte original NE
program written by the installer as EPFTUPK2.EXE. Independently extracting LX
object 4 with the checked `scripts/lx-resolve.py` decoder gives SHA-256
`40e95085630dbf3aa20fd9e21726c5b08ba5c902b27df762bbc8c1264b5e374b`.
The standalone helper probe uses its mapped OS/2 executable path as argv[0];
launching this old NE CRT through a host POSIX executable name instead caused
its command parser to report syntax error 5.

## Recovered blocker and fix

The first direct installer probe created a PM dialog, ran EPFTUPK2.EXE and
listed 25 unpacked output names, then displayed EPFIE609 (unpacking data
error). A direct helper run isolated a deterministic shutdown deadlock:
its first worker called Dos16EnterCritSec and then Dos16Exit(EXIT_THREAD,0)
without an explicit ExitCritSec. Its second worker subsequently blocked
entering that critical section, while the primary waited on its completion
semaphore. Both thread cleanup paths had retained the recursive host mutex.

The fix records ownership/nesting in the existing host-only per-thread state,
checks non-owner/underflow and overflow, and releases every owned recursive
level from both the USE32 and USE16 worker cleanup callbacks. It adds no
thread-local storage to the unloadable DOSCALLS plugin. No guest TIB layout
changes. A bounded TRACE_RET in synchronous DosExecPgm retains the host
wait/reason/result for subsequent installer diagnosis.

The original helper now completes on a fresh output directory in about one
second, unpacks all 25 files and exits 0. A re-run into existing output returned
2 with 15 unpacked files and ten existing read-only files; this was separated
from the fresh-directory success rather than classified as a new decoder bug.

## Verification

`cmake --build build-image-app-runtime -j 8` passed after the critical-section
change (`/tmp/2ine-critical-build.log`).

```
TMPDIR=/private/tmp python3 tests/integration/critical_sections.py --build build-image-app-runtime --backend unicorn
```

passed outside the sandbox. The generated LX32 and NE16 cases each create a
worker that exits while holding two nested critical-section levels and then
another worker that must acquire the same lock. The flat case joins both
workers; the segmented case uses a completion semaphore and checks that the
primary can acquire afterward. Both check underflow for a thread with no
ownership. Python formatting, Ruff and ty passed.

Actual helper runtime output and its exit status are preserved in
`build-cmd-runtime-review/install-direct-helper.{stdout,stderr,json}`. Actual
installer startup traces are in `install-direct.{stdout,stderr,json}`. A
previous collector process-group signal raced with process exit and raised
PermissionError; the subsequent process inventory confirmed no orphaned
installer/helper remained. The collector now tolerates an already-gone group.
The root job separately fixed logical shrink/regrow capacity tracking for
original module-backed NE data segments; the successful helper run includes
that prerequisite.

Full SmartSuite installation remains a separate acceptance check. Dialog
creation and successful helper extraction do not establish completion.

After the critical-section fix, a fresh original INSTALL.EXE probe logged
`DosExecPgm('C:\\INSTTEMP.EPF\\EPFTUPK2.EXE', EXEC_SYNC) -> 0, wait=1 reason=0 result=0`,
started EPFIDLDS.EXE through DosStartSession and created the `Lotus Install`
frame (`LotusSmartSuiteInstallWindow`). It then waited in an informational
`Help not available.` dialog: the existing HELPMGR WinCreateHelpInstance
always reports HMERR_LOAD_DLL because it has no renderer. The bounded probe
made no UI choices. Its eventual exit 4 followed our timeout/termination and
is not classified as an unprompted installer failure. No missing-import trap
was observed on this startup path. A successful main installer dialog beyond
this optional warning remains to be observed.

## Visible UI attempt and bounded help scope

The visible warning was not acknowledged. The unbundled CLI SDL application
was not discoverable by computer-use attachment, and attachment to a temporary
`2ine Installer Probe.app` timed out. Its automatic launch without guest
arguments exposed an independent runtime initialization crash; the root job
owns that fix and regression. No further attachment/relaunch is attempted.
The timed probe exited after its 60-second limit; ignored bundle and logs are
retained for evidence. No license or installation choice was made.

SDK `pmhelp.h` specifies a 44-byte HELPINIT with cb/ulReturnCode, tutorial name,
help table/module, accelerator/action-bar module/IDs, window title, panel-ID
flag and library name. A bounded genuine help-instance implementation would
need: checked guest strings/44-byte structure, HLP path search and existence,
validated HSP header/table spans, a real registered PM window with owned state,
application associations with correct parent/owner lookup, destruction and
per-thread/process cleanup, and dispatch for relevant HM_* help messages.
The supplied installer help file `in1o11en.hlp` has an HSP header and title
`Lotus Install Help`. No existing HLP/IPF display path was found in the current
os2libs sources (bounded search of help/IPF/HM_DISPLAY names). Creating an
opaque handle solely to skip the warning would not establish that contract.
Rendering panels/contents/index would be a separate substantial viewer job;
this work leaves HMERR_LOAD_DLL honest rather than claiming help support.

The installer is proven to unpack its helpers and create its Lotus main frame.
Proceeding past the optional warning or completing installation is unverified.
