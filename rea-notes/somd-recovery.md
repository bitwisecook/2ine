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
