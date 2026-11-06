# Native export batch handoff — 6 November 2026

The user requested a handoff, not completion of the implementation. Work is
paused with uncommitted source changes. HEAD is `99f5958`; the working tree is
shared with other jobs and contains unrelated changes. Do not reset or broadly
commit it. No installed app or user ball was updated by this export batch.

## Requirements that still govern continuation

- Implement the frozen **393 selected-native export gaps**, plus the one portable
  ABI policy gap, `DOSCALLS:323 DosQueryAppType`.
- **Do not run builds or tests until all 393 have meaningful implementations.**
  This batch has run neither. Source reads, static RE, source edits, formatting
  and generation of integration fragments are not runtime verification.
- The user explicitly permits decompiling the original OS/2 libraries with
  os2-re, REA and Ghidra, then adapting their actual behaviour into natural C
  using 2ine resources. Contract declarations alone are insufficient.
- No generic failure stubs, fake successful resources, or zero-filled SOM data
  counted as complete implementations. Export presence is not semantic coverage.
- Observe root AGENTS.md: scratch/build/test/package work stays under `build/`;
  allocate release numbers with the maintained allocator if releasing; clean
  each owned disposable subtree when its purpose ends. Preserve other jobs'
  build trees, balls and media.

## Durable inputs and evidence

The frozen worklist is [lotus-missing-native.csv](../../lotus-missing-native.csv),
with [portable-policy gaps](../../lotus-portable-policy-gaps.csv). See also
[API gap context](../../lotus-api-gaps.md), [frozen audit](summary.md),
[private-entry evidence](private-entries.md), and especially
[THK_UNITHUNK assembly evidence](thk-unithunk.md).

The complete handoff data is retained inside this checkout at:

`.cache/os2-re/lotus-export-batch-2026-11-06/`

Its `receipt.json` lists retained files and SHA-256 hashes. This is persistent
RE/interface data, not a retained build. `integration/` contains the four frozen
worklists and catalogue contracts, original-module identities, policy fragments,
declaration fragments, per-ordinal implementation manifests and PMWP data
requirements. `evidence/` preserves targeted original decompilations, original
export maps, scripts and instruction listings. References in copied manifests
to `build/exports-batch/...` are historical: use this cache instead. Some manifests
also point to the shared exact-hash `/Users/jimd/.cache/os2-re-module` cache.

The disposable `build/exports-batch/` subtree was removed after preservation.
Create a fresh purpose-named subtree beneath `build/` on continuation. Existing
`build/runtime` and `build/lotus` belong to other work and were left alone.

Originals are under `/Users/jimd/src/os2/extracted/`, not the old os2image path.
`integration/original-modules.json` records exact paths/hashes/objects/exports.
Original PMWINX is `extracted/install-media/os2image/FI/DAX/DLL/PMWINX.DLL`, SHA-256
`07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`.
The selected PMDRAG exports forward to **PMCTLS**, not PMMERGE. PMSHAPI forwards
to PMMERGE; the selected PMGPI exports have direct PMGPI bodies. MMIO forwards
to MMPM. Resolve actual import-module indices before following any forwarder.

Static provider runs used patched Ghidra 12.2 at
`/Users/jimd/src/os2/.tools/ghidra-os2-12.2`, JDK27, original bytes and the LX
loader. Root's DecompileBatch script resolves `.objectN` plus original offset.
The outputs have provisional compiler models; several thunk functions contain
bad-instruction/stack-inference warnings. Do not translate inferred pseudocode
literally without checking instructions and exact-hash metadata. The existing
THK_UNITHUNK note has stronger assembly evidence than the root SMP pseudocode.
No REA MCP tools were exposed during implementation; installed CLI and Ghidra
were used. Avoid dumping broad module-cache lookup JSON (it can embed megabytes
of documentation); filter retained JSON or query exact ordinals.

## Source state and remaining implementation

The manifests are accounting aids, not passing verification receipts. Delegated
source bodies and checked bridges are present for the 93 native PM entries,
94 Open32 graphics entries and 127 Open32 service entries. Integration is still
pending. Their provider limits and any partially connected rendering/state
behaviour must be reviewed before treating them as complete.

- **Native PM 93:** `native_pm-manifest.json` / `native_pm-policies.json`.
  Added PMWIN services/font chooser and PMGPI paths, matrices, palettes,
  metafiles, regions and state. New maintained sources are
  `pmgpi/extensions.c`, `pmgpi/paths.c`, `pmgpi/metafiles.c`,
  `pmwin/compat_services.c`, `pmwin/standard_font.c`.
  The last worker update identified saved-palette ownership, geometric line
  width/character-extra application, graphics-field/viewing-limit clipping as
  source integration work; inspect final source rather than assuming that
  update still describes every line. No build or test established correctness.
- **Open32 graphics 94:** `open32_graphics-manifest.json`, policies and
  declarations. New CMake sources are `batch_graphics_state.c`,
  `batch_graphics_paths.c`, `batch_graphics_scroll.c`,
  `batch_graphics_interop.c`; binding include is
  `guest/bindings/pmwinx_batch_graphics.h`. `GetUpdateRgn` (#377), assigned to
  services, was implemented here and should not be duplicated. Integrate
  `open32AbortCheck` into page/document output boundaries. See each manifest
  entry for remaining WMF/EMF/printer/region/provider limits.
- **Open32 services 127:** `open32_services-implementations.json`, policies and
  declarations. Source bodies cover windows/dialogs, DDE, events/mutexes,
  files/time, paths/environment, strings/atomics, menus and common dialogs.
  Include `guest/bindings/pmwinx_batch_services.h`. New files are
  `batch_services_basic.c`, `batch_services_common_dialogs.c`,
  `batch_services_drag.c`, `batch_services_paths.c`,
  `batch_services_presentation.c`, `batch_services_sync.c`,
  `batch_services_windows.c` and `atomic.c`. Three Unicode exports reuse
  existing `unicode.c` and `pmwinx_unicode.h`; preserve them. Provider limits
  include extended dialog templates/fonts, host cursor confinement, file
  creation-time writes and disk-image timestamp changes. HAL timestamp setter
  was added to shared POSIX files; review all platform coverage before build.
- **PMWP 32:** `pmwp-manifest.json` records **15 native service bodies added**
  in `pmwp.c`/`.h` and checked bridges; **17 SOM constructor/data exports remain
  unfinished**. `pmwp-data-exports.json` records genuine mutable data layouts,
  sizes and constructor requirements. PMWP class data are not callable
  functions. Implement proper guest-addressable data exports plus real SOM
  class registration/method tokens; do not add blank structures or pretend
  class constructors succeeded. Original SOM is selected in the Lotus ball;
  native `os2libs/som.c` is empty. SDK class headers are in
  `/Users/jimd/src/os2/sdk/os2tk45-master/h` and `som/include`.

Root's remaining 47 non-PMWP exports have **26 initial source bodies** and
**21 unfinished exports**. Their frozen assignment is `integration/root.json`:

- Added SO32DLL#4 `Os2LegacyGetHostId` (network snapshot, first active nonloopback
  IPv4 interface), MMIO#28/#29/#30/#77 (memory read/write/seek/header dispatch),
  all 15 PMDRAG entries (guest wire allocations, RMF matching, PS/message
  adapters and a PM message-based drag transaction), PMSPL#441 printer
  enumeration, PMSHAPI#121/#128 switch entry/title, DOSCALLS#237/#238/#292
  resume/suspend/date. Public headers, checked bindings and api32 policies were
  added for these bodies, but generated export tables have NOT been refreshed.
- **Still missing DOSCALLS:** #161 Dos16FSRamSemRequest, #162
  Dos16FSRamSemClear, #222 DosSetFSInfo, #290 DosStopTimer, #336
  DosQueryMutexSem, #351 DosStartTimer, #547 THK_UNITHUNK.
  No mutex-query ownership tracking, timer scheduler or FS label override was
  implemented. The immediately preceding investigation only read their source.
- **Still missing PMSHAPI#119 WinStartApp.** Existing DosStartSession is in
  `sesmgr.c`; existing DosExecPgm child tracking is in `doscalls/process.c`.
  Startup directories and per-launch environment need an actual concurrency-safe
  implementation rather than changing process-global cwd temporarily.
- **Still missing PMWINX 13 process exports:** #92 CreateProcess, #163
  DuplicateHandle, #192 ExitProcess, #194 ExitWindows, #201 FatalAppExit,
  #202 FatalExit, #506 OpenProcess, #681 SuspendThread, #688 TerminateThread,
  #714 WinExec, #1005 GetExitCodeProcess, #1024 ShellExecute, #1028
  WaitForInputIdle. All 13 original bodies were statically decompiled and are
  in `evidence/root-decompile/PMWINX/decompiled/`. No native process resource
  type or generic handle-duplication API was added yet.

Thus at least **38 frozen entries remain unfinished** (17 PMWP + 21 root),
before reviewing provider limits and integration in the source-body groups.
Do not report all 393 implemented or begin testing yet.

## Root infrastructure changes that need review

- `guest/bindings/doscalls.h` now has checked DosQueryAppType input-string and
  4-byte output marshalling. Its api32 policy is 8 stack bytes, callee_pop0,
  LX_ABI_STACK32. This addresses the one portable policy gap in maintained
  source; regeneration/verification remain pending.
- `lx_execution_atomic_u32` now supports mode2 add, returning the previous
  word; mode0 read and mode1 exchange are preserved. Unicorn serializes with
  its CPU mutex; native i386 uses `lock xaddl` for x86 unaligned semantics.
  The runtime validates modes0..2. Open32 Interlocked bridges use this.
- Thread suspension added an execution-checkpoint callback to runtime state,
  invoked outside the CPU lock at bounded Unicorn execution slices and from
  poll-termination. DOS threads gained a suspend monitor; primary thread1 has
  its own control. CREATE_SUSPENDED is now admitted. Current direct-native
  x86 suspension explicitly returns ERROR_NOT_SUPPORTED (resume is allowed).
  Review races, lock ordering, cancellation, primary/NE-thread handling and
  cleanup before treating this implementation as done. No test was run.
- Added typed handle enum values, appended only: PM palette/metafile;
  Open32 defer/event/mutex/DDE data/DDE conversation/enhanced metafile; timer.
  Timer kind is reserved, no timer engine exists yet. Preserve enum ordering.
- CMake gained only the pmdrag→pmwin dependency from root's new calls. The
  delegated new source lists are NOT globally integrated yet.

## Required final integration sequence

1. Finish the 38 outstanding exports and review partial behaviour/provider
   limits. THK_UNITHUNK requires a genuine register-aware bridge and 32→16
   dispatch, not a guessed fixed C signature. Read its assembly note first.
2. Merge all policy fragments into `guest/bindings/api32.json`; add correct
   api16 metadata for the RAM semaphores. Preserve concurrent Unicode, pointer,
   priority, clipboard and display-startup changes already in this shared tree.
3. Merge exported declaration fragments into `os2libs/pmwinx.h` with the usual
   OS2EXPORT/OS2APIINFO syntax, and binding includes into main guest binding.
   `tools/lxapigen.py` parses public header text, not preprocessor includes.
   Implement proper data-export infrastructure for PMWP as required.
4. Integrate CMake new sources. Wire `open32CloseSync` into CloseHandle and
   `open32WaitSync` into WaitForSingleObject; declarations are in
   `pmwinx/batch_services.h`. Also integrate sync/deferred/dialog cleanup,
   thread-queue registration/unregistration, cursor clipping and window-update
   locking hooks declared there, plus the graphics printing abort hook.
5. Regenerate glue with `python3 tools/lxapigen.py` after maintained inputs are
   complete. Audit actual per-ordinal behaviour and guest marshalling against
   all 393 frozen rows plus the portable policy gap. Generation is not testing.
6. **Only after all entries are implemented**, build and run relevant tests,
   fix failures, perform actual Lotus tab/resize/quit/SmartMaster regressions,
   then clean the owned build subtree and retain small receipts. Existing
   earlier tests/screenshots describe the prior SmartMaster fixes only, not
   this much larger unbuilt batch. Do not reuse their success as batch evidence.

## Original problem context

Lotus 1-2-3 on 2ine0.4(18) had corrupt second-tab labels, fixed-size windows,
nonfunctional quit, then missing PMWINX#630 when creating a loan-amortization
SmartMaster. Earlier work (before the no-tests instruction) improved tab layout,
resizing, WMF/text/mapping support, worker error exits and dependency diagnostics.
[SmartMaster notes](../../lotus-smartmaster.md) record that earlier verification.
Missing imports/guest faults now log and exit the worker with status1 rather
than SIGABRT, leaving the separate launcher alive. An underlying guest teardown
fault and native physical maximize/resize verification were still outstanding.
