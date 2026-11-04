# Missing original SOM runtime and dependency-media recovery

The 2026-11-03 report concerns Word Pro. Read-only inspection of the actual
`Documents/Lotus.2ineball` found none of SOM/SOMD/SOMSEC/SOMEM/SOMS/SOMU/SOMTC/SOMIR
under C:\OS2\DLL. It also has no `os2-media.json` or dependency-audit history;
its D: drive is an ejected CD device. The latest supplied log reports the
separate 16-bit alias issue and LTOLEO20's SOMD failure. The alias loader fix is
owned by the root agent; dependency recovery does not solve that loader contract.

The current private Word Pro acceptance balls already contain the eight original
SOM libraries. Their success therefore did not establish that an older user ball
had been repaired. Existing code correctly treats SOM as an original dependency,
rather than claiming its empty native module supplies the SOM object runtime.

## Original private-copy proof

`somd-client-recovery-suf1b_vm/Probe.2ineball` is a new copy of the Documents
ball, with non-C mappings detached and runtime cache/logs/devices excluded. No
Documents-ball write or desktop input occurred. Before recovery, scanning 405
actual installed binaries found SOMD missing from LTOLEO20, LTSOMO10 and LTOLSO20.

Recovery from the user's Warp 4.52 client ISO restored 15 original files in
105.2 seconds on its first index. This includes all eight SOM libraries, plus
help resources/viewers and Unicode dependencies. `recovery.json` records SOMD
found at C:\OS2\DLL\SOMD.DLL, including its circular SOMSEC dependency.
`som-closure-hashes.json` matches every original hash already qualified in
[lotus-123-startup.md](lotus-123-startup.md). Seventeen optional Notes/database/
network imports remain missing across the whole installed suite; two NETAPI
duplicate-version ambiguities were reported and preserved, rather than choosing
an arbitrary version. This is not a claim of complete suite compatibility.

The generated LX `SOMDLOAD.EXE` then calls real DosLoadModule("SOMD") and
DosFreeModule, checking both return codes. It prints `SOMD_LOAD_OK` and exits 0
using one coherent Modal Acceptance runtime closure with dummy video/audio.
`somd-load-{plan,result}.json`, stdout and stderr are retained alongside recovery
results. This verifies actual original module initialization/import resolution,
not merely a filename in the audit. Full Word Pro UI/DSOM service behavior is
outside this probe.

The initial raw 719-MiB server ZIP backend audit exceeded a bounded 240-second
diagnostic timeout while indexing. No recovery success is claimed for that run. New
frontend/CLI selections first use `open-os2-source` to detect embedded ISO files
and the installation file set, before requesting dependency recovery. A nested
ZIP→ISO→ZIP fixture verifies actual SOMD restoration through that path.

The same real ZIP exposed a separate source-selection bug: its first embedded
WSEB_CP2-BootCD.iso has a primary volume descriptor and El Torito record but no
volume-descriptor terminator before path-table data. The strict ISO reader
rejects it. A readable WSEB_CP2.iso follows it. Previously, selecting the archive
failed while browsing only the first detected image. Source opening now validates
detected ISO members, returns readable choices and explicit skippedSources
warnings, and fails if none are readable. It does not relax ISO validation.
Native/GTK source and recovery views display the warnings; CLI output retains
them. The real ZIP now normalizes to WSEB_CP2.iso/OS2IMAGE in 0.37 seconds with
--no-resolve; this is source-selection/static-audit proof, not full server-media
recovery. Artifact: somd-recovery-3g5f8dbg/cli-normalized-zip-check.json.

## User path and changes

macOS Check and Restore System Libraries first audits remembered/attached media.
If imports are missing and no verified OS/2 source is available, it offers a
native media chooser, then restores only the missing dependency closure. GTK
has the corresponding Applications button/main-menu action and native chooser.
Both normalize embedded ISO sources and retain the Activity summary/audit log.
They do not select full operating-system extraction or overwrite ambiguous files.
The actions are guarded while a guest/other operation is active. Cancellation
keeps the initial audit; no recovery request is made.

CLI:

```sh
python3 -m tools.ball --ball Office.2ineball audit-dependencies --source /path/to/OS2.iso
```

`--source` can be repeated. The legacy support-only import now also remembers its
selected OS/2 media, matching category extraction, so later dependency recovery
can find the original source. Remembered paths are not treated as trusted OS/2
payload: command/version signatures and actual binary formats are checked again.

Verification: all 169 tool tests pass (21.285 seconds), including the 33
dependency/support tests, including actual nested
media, unchanged drive mappings, source history, transitive/circular imports,
missing/unverified-source chooser policy and ambiguity preservation. The isolated
Swift build compiles the complete native app; both focused dependency chooser/warning
state tests pass. Ruff/ty, C formatting and Python syntax checks pass. GTK cannot
be launched on this host because PyGObject is unavailable; live chooser input is
not claimed. Logs: `/tmp/2ine-somd-all-tools-final.log` and
`/tmp/2ine-somd-swift-source-final.log`. Reproducers are ignored diagnostic scripts under
`build-lotus-visuals/probe-somd-{client-recovery,load}.py`.

## Subsequent actual-ball repair

After the repeated missing-SOMD report, recovery was run against the actual
`Documents/Lotus.2ineball`, using the same verified client ISO. It recovered 15
files, including all eight qualified SOM hashes, with zero audit errors. The
saved manifest, CONFIG.SYS and standard profile hashes were unchanged. Its audit
was saved inside the ball, with a separate diagnostic copy under the ignored
`build-lotus-visuals/user-som-repair/` directory.

The user then deleted that ball before the isolated 1-2-3 startup check could
begin, and confirmed the deletion. It was not restored. The repair's file/hash
checks passed; startup of that particular repaired ball was therefore not
verified. The user intends to rebuild it after installing the updated manager.

## Rebuilt-ball private-copy recovery, 2026-11-04

The rebuilt Documents ball has 397 Lotus EXE/DLL files, no original SOM DLLs,
and a stale09:41 audit that scanned only11 OS/2 binaries. A fresh read-only
source copy is `build-som-current/Probe.2ineball`; only its private copy is
audited or executed. External drives, old runtime cache, logs and IPC are not
inherited. `audit-current.py` binds the current unified ball library.

The no-resolve audit actually scans409 binaries and reports22 missing modules,
including SOM, SOMD, SOMIR and SOMTC. It records the failing original imports
from LTOLEO20, LTSOMO10, LTOLSO20 and other Lotus component DLLs. It has zero
audit errors, unreachable imports or disabled libraries. Exact result is
`build-som-current/before.json`; this proves the static scanner detects the
relevant failures when run after the application payload exists.

Recovery uses the ball's remembered normalized server source
ZIP → WSEB_CP2.iso → os2image. It completes in31.99 seconds, restores11 files,
and scans420 binaries. The eight original SOM/SOMD/SOMSEC/SOMEM/SOMS/SOMU/SOMTC/
SOMIR DLL hashes all match the qualified originals in
[lotus-123-startup.md](lotus-123-startup.md). Exact sizes, destinations and all
eight SHA256 values are retained in `build-som-current/som-closure-hashes.json`.
The other restored files are LIBCS.DLL, LIBUNI.DLL and UCONV.DLL. Zero audit
errors, unreachable imports or disabled modules remain.

Seventeen imports remain missing across the installed suite:
`$BSAFE, $COMPUTE, $LIB, $NIF, $NSF, $REPLSUB, $ROUTERL, $SDK, INOTES,
NETAPI32, BTRCALLS, DB2APP, DB2UTIL, LIBCT, ORA_D73O, ISQLI501, NETAPI`.
These Notes/database/network dependencies are not supplied by this verified
closure. Exact original import locations, media provenance and recovery report
are in `build-som-current/recovery.json`; no complete-suite claim follows.

The original 1-2-3 startup probe then passes against a second isolated copy:
genuine named 1-2-3 frame plus WinGetMsg, no missing imports, failed dependencies
or guest CPU-stop markers. Evidence is
`build-som-current/123-startup-with-logs/{result,plan,original-inputs}.json`
and stderr.log. Return84 is after the deliberate bounded stop, not an ordinary
application exit. The initial harness failed before guest execution because
prepare-launch's WPS sync tried saving a log into the excluded logs directory.
The private harness copy creates that directory; the source/UI integration
defect was reported to the root agent and is not hidden as a guest failure.

### DSOM service scope

Static DLL recovery does not restore SOMDD.EXE, SOMDSVR.EXE, OS2/ETC/SOM.IR,
the DSOM repository files SOMDIMPL.DAT/TOC and SOMDCLS.DAT/TOC, or the SOMD,
SOMK and SOME_OS2 message files. A bounded original-media inventory of those ten
files with paths/hashes is retained as
`build-som-current/dsom-companion-inventory.json`. Their presence as original
companions is distinguished from proof that every message or repository file
is independently necessary for every operation.

The prior workbook interaction evidence established an actual DosOpenQueue
wait until the original SOMDD daemon runs; that service launch prepends
C:\OS2\ETC\SOM.IR to SOMIR and uses C:\OS2 as cwd. SOMDSVR is the original
server helper. The fresh startup-only checkpoint passes without that daemon;
it therefore does not qualify workbook/OLE/DSOM service operations. No service
provisioning code, real-ball mutation, GUI input or licence decision was made
in this investigation.

## Audit freshness and shared launch preflight

The stale11-binary success result is now treated as historical evidence rather
than a current status. New `ball/dependency_freshness.h` fingerprints the
sorted installed candidate inventory using the audit's extension filter and
100000-entry/30000-binary bounds. C:\OS2\DLL\2INE is excluded. Native metadata
includes file identity, size, modification/change timestamps and their available
fractional fields. Virtual files fall back to exposed size/timestamp metadata.
Declared ancillary component paths include their existence/metadata, so removal
of a SOM repository or message file also invalidates the report.

The token additionally binds CONFIG.SYS bytes, manifest environment/system/
runtime/drives/startup-program definitions, remembered OS/2-media record bytes,
physical source metadata (or bounded folder metadata), external LIBPATH
directory candidates and selected runtime capability/build declarations.
Configuration declarations are capped at1MiB each. This is a bounded metadata
cache validation, not a cryptographic assertion of every binary's contents or
an atomic filesystem transaction. Symlink targets are not followed, matching
the audit. Metadata failure/limits leave freshness unverified and the report
stale/incomplete.

Reports and compact latest records persist `fingerprint`, `runtimeBuild` and
`stale` plus media/component metadata. Tokenless older reports and changed
inventories appear in snapshots as stale, completefalse, with an explicit stale
summary. A no-import audit compares metadata tokens before/after the scan;
changes during that bounded pass persist a stale/incomplete result with an
explicit explanation, without retries or sleeps. Recovery necessarily adds
known files, so its fingerprint is taken after recovery/service provisioning;
supervised installer completion remains necessary for that nontransactional
boundary.

`ensure-dependencies` returns the full current/cached audit shape and a
`cacheHit` boolean. Missing/stale reports refresh automatically with remembered
media. A fresh no-resolve report that still has missing imports receives one
automatic recovery attempt; a fresh recovery-enabled report with optional
missing imports is reused. Explicit sources force a new attempt. Explicit
`audit-dependencies` always runs. The public prepare-launch dispatch invokes
the same preflight once and attaches compact `dependencyAudit`; recursive
startup-service preparation does not rerun it. Optional missing modules remain
visible without blocking every unrelated application.

Six persistent tests in `tests/tools/test_dependency_freshness.py` cover old
tokenless reports, files appearing after an earlier empty installer audit,
SOM recovery, reuse with optional missing modules, one recovery after a
no-resolve audit, candidate replacement, CONFIG/settings/media changes,
runtime declarations, cache/data exclusions and the launch-plan attachment.
Combined freshness/dependency checks pass25/25; an earlier related portfolio
passes46/46. Ruff, ty and diff checks pass. Shared-header formatting is
coordinated with the concurrent DSOM companion work.

In the fresh private420-binary Lotus copy, a no-resolve refresh takes0.367s;
the next ensure reuses the same full report in0.016s. Exact results/timings are
`build-som-current/freshness-current-{0,1}.json` and
`freshness-current-timing.json`. That timing proves bounded cache validation
on this fixture, not a universal performance guarantee. No Documents-ball
write or GUI input was performed for freshness verification.

## Integration repair of the rebuilt Lotus ball

After the isolated checks, the parent task restored the authorized current
`~/Documents/Lotus.2ineball`. It first checked for active guest workers and
retained copies of ball.json, CONFIG.SYS, remembered media and both profile
files under `build-som-current/live-before-recovery`. The source opener expanded
the remembered ZIP → WSEB_CP2.iso → OS2IMAGE source to the four verified roots
described in os2-component-groups.md; the application's CD mapping was preserved.

The shared recovery operation restored 21 files: eight original SOM DLLs,
LIBCS/LIBUNI/UCONV and ten verified DSOM package companions. All eight SOM DLL
hashes match the previously qualified originals. It provisioned the owned
`os2-som` startup record with `C:\OS2\SOMDD.EXE` and inherited SOMIR, preserving
manual records and existing repository data. The resulting current audit scans
422 binaries, reports no unreachable imports, disabled libraries, missing
package companions or audit errors, and retains 17 missing optional
Notes/database/network imports. This is not a claim that every Suite feature
or remote OLE workflow is operational.

Exact preflight, recovery and independent verification records are
`live-before-audit.json`, `live-recovery.json` and
`live-recovery-verification.json` under build-som-current. The repair script is
`restore-live-som.py`; no original payload is tracked in Git.
