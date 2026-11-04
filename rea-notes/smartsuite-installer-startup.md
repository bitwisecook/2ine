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

## Native DLL filename and OS-level check on macOS

A subsequent run with the user's original media and a separate copy of the
Lotus ball reached the licence agreement after acknowledging the optional help
warning. The original CMD.EXE reports version 4.50 and revision 14.089 for
`VER /R`. The installer also loads PMWINX, queries its module filename, and
queries that file's modification date. Its `CITOS2_CheckOSLevel` routine requires
the PMWINX date to be at least 1997-12-20 after accepting the version/revision.

The trace returned the bare module name `PMWINX` to DosQueryPathInfo, which could
not identify a file. macOS dyld had resolved the loader's nonexistent relative
`./libpmwinx.so` through its runtime search paths before the explicit search
reached the ball's native-library snapshot. The loader now requires the candidate
file to exist before opening it, retaining the explicit search order and the
actual guest-visible snapshot filename.

The generated guest in `tests/integration/ball.py` now loads PMWINX, queries its
filename and uses that filename to query file metadata. Against app build 3 it
failed with guest status 102 at the metadata query. This regression exercises
the prerequisite without accepting a licence or installing SmartSuite.
The rebuilt app (build 4) passes the complete ball integration harness with
the Unicorn backend and strict signature verification. A complete original
SmartSuite installation after the fix remains unverified.

The original EPFIDLDS.EXE was then launched with build 4 in the separate test
ball. After acknowledging the optional help warning, its trace queries
`C:\OS2\DLL\2INE\unicorn\de2b8b50d1a2a9ff\libpmwinx.so` instead of the bare
`PMWINX` name and reaches the Lotus licence agreement. The copied library is
424,752 bytes with a 2026-11-02 modification date. Evidence is retained under
`build-lotus-version-review/fixed.stderr` and `verification.json`. Continuing
past the licence agreement requires the user's explicit acceptance approval.

The user subsequently continued this isolated installer interactively. The
trace progressed through application selection, disk-space warnings, program
folder and CONFIG.SYS choices, and Begin Copying Files. It launched
`C:\PROBE\EPFIDLD2.EXE` with the generated INSTALL.RSP, then eventually exited.
This confirms the original OS-level rejection was cleared. It does not confirm
installation: neither the test ball nor the original Documents/Lotus.2ineball
contains LOTUSW4 or any suite application executable, and the test CONFIG.SYS
remains unchanged. The later installer process did not copy the suite payload.
Its startup/copy-stage behavior remains the next blocker; the earlier free-space
warnings alone do not establish the reason it stopped.


## Application checks, page repainting and copy-stage startup

The user's report of missing application ticks and retained wizard pages exposed
separate PM gaps. Lotus uses its own owner-drawn application list and crops the
checked and unchecked cells from SBMP_CHECKBOXES (WinGetSysBitmap id 48). That
system bitmap was unsupported. It now supplies an independently drawn four-column,
three-row checkbox/radio atlas. A pixel regression draws the same top-row cells
used by the original installer and verifies their checked states differ.

Hidden, moved and destroyed child pages also left their old pixels in the shared
host surface. These operations now invalidate the exposed parent rectangle and
repaint affected children. A regression paints a blue child over a red parent,
then checks the actual pixels after hide, show, move and destruction. Both new
regressions failed before the fixes and pass afterwards.

The original EPFIDLD2.EXE starts timer 2 with a zero millisecond interval. PM
previously rejected zero, leaving its copy thread waiting indefinitely for the
startup event. WinStartTimer now treats zero as an approximately 55 ms tick,
as specified by the original Presentation Manager reference:
https://bitsavers.org/pdf/ibm/pc/os2/OS2_1.x/Microsoft_-_OS2_Windows_Presentation_Manager_Reference_Volume_2_1987.pdf
A regression checks that the zero-interval timer starts, delivers WM_TIMER and
can be stopped; it failed before this change. The original copy worker now
posts its startup event and launches EPFINSTS.EXE with the user's saved response
file. That child first displays another optional missing-help notice.

After the user's explicit approval to run the isolated probe, the child installer
was launched directly with those arguments and the optional notice dismissed.
Its original Install - progress window now shows a Transferring bar, and it
creates C:\LOTUSW4 and writes payload files there. This is evidence of actual
copying; full suite installation and application startup remain unverified.
The isolated probe logs are engine.stderr and copy-ui.stderr under the ignored
build-lotus-version-review directory. The seven PM/dialog/window/thread/Open32
regressions pass with SDL's dummy drivers and the rebuilt app packages successfully.


The standalone engine probe copied approximately 38 MB, then displayed EPFIE232:
an expression contained an empty operand in `(%SECOND_LANGUAGE% == 0)` after
substitution. SECOND_LANGUAGE is absent from the saved INSTALL.RSP; the original
main wizard contains a separate initialization of that shared installer variable.
Therefore this standalone probe does not establish a new emulator defect. A
fresh, complete wizard run is required to distinguish missing probe initialization
from a runtime compatibility failure. No package expression or media file has
been patched to bypass the check. The error window is retained for inspection.


The user approved and manually advanced a fresh full-wizard run with build 6 in
LotusFullProbe.2ineball, accepting the licence and retaining local defaults.
The main wizard's owner-drawn list now calls WinDrawBitmap with a valid bitmap
handle (65852), instead of zero. A screenshot at the folder-name stage shows
only the current page; previous pages have been cleared. The full flow launched
EPFIDLD2 and EPFINSTS, and the user dismissed the child's optional help notice.
Copying passed 40 MB without reproducing the standalone engine's EPFIE232 error.
This supports missing main-wizard initialization as the standalone probe limitation.
Full installation and application startup are still being checked.


The fresh full wizard completed successfully: EPFINSTS reported "All files have
been transferred successfully" and requested a reboot; the main installer exited
0. The user confirmed seeing this completion message. LOTUSW4 contains 1,693 files
and 154,602,960 bytes, including all five application executables. CONFIG.SYS now
contains the suite PATH/LIBPATH, HELP, SOM and application environment entries.
A new launch reads this updated configuration without rebooting macOS. The
original Documents/Lotus.2ineball was not replaced by this isolated installation.
Full evidence is in build-lotus-version-review/full-install-verification.json.

After explicit user approval, Word Pro was launched without tracing from the
installed ball. The native window "Word Pro - Untitled1" was observed rendering
text and a table entered by the user. This verifies startup and visible drawing,
not document save/reopen. Bounded startup checks used private settings copies:
Freelance encounters the loader's unsupported LX export-bundle type at
loader/load_lx.c (OVRFLWKR.DLL exists); 1-2-3 and Approach fail on missing OS/2
SOM dependencies such as somd/somtc; Organizer remains running but repeatedly
reports missing somd through LTOLEO20/TZBACK32. Those startup limits are separate
from successful suite installation and are retained in application-startup.json.

The later [Lotus export/startup investigation](lotus-startup-exports.md) implements
LX forwarded exports and the first Open32 startup calls. It reproduces and clears
the reported worker-DLL and GetACP failures; all three applications still have
later startup blockers recorded there.

## Progress-dialog flicker and unpacking performance

A native PM regression reproduces premature presentation: repainting a parent,
waiting 25 ms and then retrieving its child's pending WM_PAINT presented the
parent's cleared background before that child had redrawn. The previous 16 ms
fallback expired during slow guest paints. The regression failed before the fix.
Each damaged window now tracks whether its paint request has been offered to
the application. Presentation waits for previously unoffered, visible child
damage; the timed fallback remains for an application that defers a delivered
paint request. Hidden ancestors do not block presentation, and self-invalidating
paint callbacks retain the fallback. The delayed parent/child regression and a
separate deferred-paint check both pass. All seven PM/dialog/window/thread/Open32
checks pass, and app build 7 packages with a valid strict signature. The original
full installer has not been rerun after this final flicker fix.

The diagnostic install produced 1,422,671 trace lines (89.7 MiB). However, a bounded
original EPFIUPK2 benchmark decompressing LWPIENVO.DL_ produced the same 189,952-byte
DLL in 0.306/0.313 seconds without tracing and 0.316 seconds with TRACE_NATIVE.
Tracing therefore does not explain the main unpacking cost in this sample.
The no-argument helper exited in 0.024 seconds (0.019 CPU seconds); an absent
source-file lookup took 0.139 seconds (0.132 CPU seconds); decompression took
0.309 seconds (0.297 CPU seconds). These are small-sample measurements, not a
full-install speed comparison. A five-second CPU Profiler capture of the larger
WORDPRO.EX_ unpack is dominated by Unicorn execution and memory helpers:
helper_check_exit_request_x86_64 29.8% of sampled cycle weight, store_helper 13.0%,
block_boundary 8.4%, address_space_translate_internal 8.0%, flatview_translate_x86_64
5.9%. The bounded profile ended by its time limit and is not an unpack completion
check. No CPU-budget/termination checks were weakened or bypassed. Installation
speed remains a measured emulator-performance limitation; this change fixes the
reproduced paint scheduling bug without claiming a decompression speedup.

## AArch64 execution optimizations

Follow-up profiling isolated two avoidable costs in the embedded Unicorn backend.
The normal exit-request check called a C helper despite no pending exit. Native
AArch64 emission now tests the same signed counter flag at every existing check
site and calls the original helper when an exit is requested. This preserves
guest register synchronization and cooperative termination checks. A bounded
20-million-iteration CPU loop improved from 0.6185 to 0.5281 seconds in the initial
exit-only comparison, but original Lotus unpacking improved by only about 3%.
UC_TLB_VIRTUAL and a helper-call annotation were also tried and discarded: the
former slowed unpacking substantially and the latter had no measurable benefit.

A subsequent 20-second CPU Profiler capture of original WORDPRO.EX_ unpacking
showed store_helper at 23.3% of cycle weight and repeated address-space/region
tree lookups at 34.4% combined. The memory helpers now reuse the RAM region
already resolved during page translation. The pointer lives in CPUIOTLBEntry,
following the same invalidation and victim-entry movement as the cached host
address. Protection checks, dirty/code tracking and fresh lookups after hooks
remain; snapshots and MMIO use the original region lookup. No guest execution
budget or API/exception interception was relaxed.

Both changes are generated in a private build-tree source copy by
hal/emulator/unicorn_optimizations.cmake. The supplied upstream tree is unchanged.
They apply only to AArch64 embedded builds with matching implementation hashes;
other hosts, unknown sources and system libraries use the original implementation.
TWOINE_UNICORN_OPTIMIZATIONS=OFF disables the overlay. Manual CMake checks cover
disabled, unknown-revision, incomplete-source and non-ARM fallback, plus unchanged
configure timestamps. CPU regressions verify live registers at a write fault,
resumption after protection changes, unmap/remap and exact block-budget progress.

The signed macOS app build 13 passes all 53 project CTests and 81 applicable
upstream Unicorn memory, x86 and control checks. One upstream test using an ARM64
guest is excluded from this x86-only build; its initial unfiltered invocation
failed because that guest architecture is unavailable. Snapshot, MMIO, virtual
translation and memory-hook tests are included in the passing checks. Build and
test evidence is under build-perf-lotus/optimized-build.log, optimized-tests.log
and upstream-tests-final.log. The prior Word Pro session was left open; it keeps
its original runtime until relaunched. A full installer timing has not been
repeated after these optimizations.

The final comparison uses the original EPFIUPK2.EXE and compressed ISO files in
LotusFullProbe.2ineball, comparing preserved signed build 7 with signed build 13.
Tracing is disabled and build order alternates. Each destination file is deleted
before its timed run; all 24 runs exit 0, recreate the output and match the
installed-file SHA-256. The first round warms both builds; the following five
rounds give these median elapsed times:

| Original input | Output bytes | Build 7 | Build 13 | Less elapsed time |
| --- | ---: | ---: | ---: | ---: |
| LWPIENVO.DL_ | 189,952 | 0.26281 s | 0.23968 s | 8.8% |
| WORDPRO.EX_ | 4,542,816 | 3.42938 s | 2.35244 s | 31.4% |

Results and the isolated comparison script are retained in
build-perf-lotus/optimized-fresh-comparison.json and compare-optimized-fresh.py.
An earlier comparison that overwrote existing destinations gave 23% and 30%
reductions, respectively; the fresh-output measurements above are the final
reported values. Some large-file runs show substantial host timing variation
(baseline up to 5.82 s, optimized up to 3.07 s after warmup), so these are local
sample medians rather than a prediction of whole-install or application speed.

## Remaining rendering failures reported after 0.2

The user's report that progress text disappears except when its timer ticks
matches a specific sequence in full.stderr (for example around line 1419788):
the installer explicitly sends WM_PAINT to its progress dialog with an empty
update region. acquirePresentationSpace treated that empty region as the whole
window, so the dialog cleared itself and erased unchanged child labels. It also
reported nonempty damage to the caller and never clipped drawing to damage.
WinBeginPaint now snapshots the actual update rectangle before validating it,
including an empty rectangle, and retains that paint clip separately from the
application's GPI clip. Window fill, raster, text and accelerated line output
honor that clip. WinEndPaint removes the temporary clip from persistent spaces.
The [IBM PM reference](https://ftpmirror.your.org/pub/misc/bitsavers/pdf/ibm/pc/os2/OS2_2.x/IBM_OS2_2.0_Technical_Library_1992/S10G-6265-00_OS2_2.0_Presentation_Manager_Programming_Reference_Volume_2_199203.pdf)
describes WinBeginPaint clipping to the update region. A regression sends the
same unsolicited paint and then partial fill/line paints over a parent with an
unchanged child. It fails before the fix and preserves the child afterward.

The black backdrop has another concrete failed call in that trace: after filling
black, Lotus requests GpiCreatePS with 0x5014 (PU_LOENGLISH | GPIT_MICRO |
GPIA_ASSOC). Only pixel units were admitted. Fixed physical page units now map
through the renderer's logical 96-dpi display, including metric, English and
twip units, with forward/inverse conversion tests. Explicit viewport mapping
remains supported, and WC bitmap destinations use world-to-device conversion.

SS_BITMAP statics previously accepted a bitmap style but drew no image and did
not bind their resource ordinal. They now share the existing validated bitmap
decoder with GpiLoadBitmap, hold a typed bitmap lease, draw the image and release
owned resources correctly. SS_AUTOSIZE no longer interferes with static type
recognition; zero-sized image controls also acquire their natural size. Stock
information/question/error/warning icons can now be drawn, and message boxes
use their requested icon. Native checks cover image pixels, autosizing, pinned
bitmap lifetime and copied/shared system icon handles.

An offscreen probe independently extracted the unchanged EPFIDLDS.EXE resource
pages, decoded all six bitmap resources (including the Lotus logo tile and
40x40 clock), and loaded original dialog 4148 with its bitmap bound. No old
installer code or licence action was executed by this resource-only probe.
Evidence is in build-perf-lotus/original-image-verification.log and
lotus-resources/; the original copying wizard has not been rerun visually after
these changes. App build 15 packages the fixes. All 53 project CTests pass;
installer-rendering-tests.log and installer-rendering-app-build.log record the
checks. The help-manager limitation remains: there is no IPF/HLP panel renderer,
so WinCreateHelpInstance still reports HMERR_LOAD_DLL and the optional warning
is expected.
