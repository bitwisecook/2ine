# OS/2 media groups and original DSOM package recovery

The extraction groups are append-only. Existing IDs and indices remain
`cmd=0`, `fonts=1`, `assets=2`, `printers=3`, `help=4`, `full=5`. New IDs are
`som=6`, `runtime=7`, `multimedia=8`, and `network=9`. The full-set selector uses
a named constant rather than a six-entry selection array. Old saved choices
keep their meanings.

`ball/os2_groups.h` supplies shared metadata. A plan's `groups` array contains
`id`, `title`, `detail`, `defaultSelected`, and `requires`. Only command support
is selected by default. SOM requires the runtime group. Import responses and
`os2-media.json` store expanded `selectedCategories`; remembering media or
automatic dependency recovery preserves the saved selection. The CLI accepts
all ten IDs through repeatable `--include` options.

## Original source boundary

The original eight-DLL SOM closure is SOM, SOMD, SOMSEC, SOMEM, SOMS, SOMU,
SOMTC and SOMIR. Original installation packages also contain SOMUC and OREXXSOM;
these exact names belong to the SOM group. LIBCS, LIBUNI and UCONV form the
runtime group, supported by the prior real eleven-library restoration recorded
in `rea-notes/somd-recovery.md`. Arbitrary SOM-prefixed application DLLs are not
classified as system runtime files.

The DSOM companion inventory is hash-bound in
`build-som-current/dsom-companion-inventory.json` and contains these original
installation destinations:

- `C:\OS2\SOMDD.EXE` and `SOMDSVR.EXE`;
- `C:\OS2\ETC\SOM.IR`;
- `C:\OS2\ETC\DSOM\SOMDIMPL.DAT`, `SOMDIMPL.TOC`, `SOMDCLS.DAT`, `SOMDCLS.TOC`;
- `C:\OS2\SYSTEM\SOMD.MSG`, `SOMK.MSG`, `SOME_OS2.MSG`.

The original extracted package paths are under
`/Users/jimd/src/os2/extracted/module-packages`: SOM.IR and core libraries in
`6ece66246120b49415e84ca4f75cbe814bff7c4f33ee7649c87d53d5da17b7a9`,
daemon/server programs in
`bd2b204ad0b5ac7835f5526230cf0bcee4c8988f5c0229e05f5b3ccea77ef729`,
repositories/messages in
`305fe42ca174ccffe059cf863be14f7f82893e01cbfb3c31a27b688a111787ed`.
SOMUC is `output.019.OS2_DLL_SOMUC.DLL` in the first package; OREXXSOM is
`output.006.OS2_DLL_OREXXSOM.DLL` in
`d55671cfcd1deec318f9a0b754c483572f99134addbf17d901ed397641373428`.
No separate SOMD environment file was established by this inventory. No
original payloads are committed.

Multimedia and networking classification uses recognized original MMOS2,
TCPIP, MPTN, IBMLAN and IBMCOM install paths. Existing font, image/sound/video,
printer and help classification takes precedence. Native-provided and disabled
system modules remain excluded by the existing provider policy. Extracting
these files does not enable original network or hardware drivers and does not
establish general original service compatibility.

## Dependency and owned-service contract

An audit's `componentGroups` contains only evidence-based missing/unreachable
SOM/runtime requirements. Each record has the shared group metadata plus
`modules`, `requiredBy`, `files`, and `requiredByGroup`. A required runtime group
can have an empty module list: that means dependency on the selected SOM group,
not an invented missing static import.

When SOMD is imported or installed, `componentFiles` separately lists the exact
ten DSOM package files and found/missing state. This avoids treating binary
repositories or messages as DLL imports. Missing package files make the audit
incomplete even when SOMD.DLL exists. Verified media recovery includes them,
validates executable companions as OS/2 LX/LE/NE, uses the same bounded staged
extraction for data, and preserves existing user repository bytes. Version-two
media indexes prevent old DLL-only caches from hiding new companions. These
are verified package companions; no claim is made that every message file is
individually essential for every DSOM operation.

Once SOMD and all ten companions exist, explicit SOM/full extraction or an
automatic recovery audit adds a `startupPrograms` record owned by `os2-som`.
It runs `C:\OS2\SOMDD.EXE` from `C:\OS2`, uses the existing per-launch process
ownership and teardown path, and sets service-only
`SOMIR=C:\OS2\ETC\SOM.IR;%SOMIR%`. Self-reference is expanded by the existing
environment overlay against the current receiving launch. Later Lotus
repository additions remain visible. SOMBASE inherits an existing setting or
defaults to `C:\OS2`. Existing user-owned SOMDD startup records take precedence;
other startup records are preserved and repeat imports do not duplicate them.
There is no host-global daemon installation. A complete package without a
service can be repaired without marking the audit stale solely for its own
intentional manifest write.

Freshness fingerprints include declared companions and startup definitions.
Deleting a repository or removing the managed service invalidates the cached
audit. Reports retain component information in JSON and human-readable logs.

## Verification

```sh
cmake -S ball -B build-groups-current -DCMAKE_BUILD_TYPE=Debug
cmake --build build-groups-current -j6
TWOINE_BALL_LIBRARY="$PWD/build-groups-current/libtwoine_ball.dylib" python3 -m unittest \
  tests.tools.test_os2_support tests.tools.test_os2_dependencies \
  tests.tools.test_wps_applications tests.tools.test_dependency_freshness \
  tests.tools.test_ball_engine
```

The focused 72-test run passed in 34.101 seconds before two final edge
assertions. Tests cover group isolation/dependencies/defaults, flat and nested
paths, provided-module exclusions, full extraction, user repository/service
preservation, late SOMIR inheritance, missing package reporting and repeated
recovery. The WPS regression removes the disposable ball's logs directory and
successfully prepares a registered launcher while recreating its registration
report directory. Clang-format 21 dry-run, Ruff and ty pass for owned files.

Real-media acceptance is private and headless in
`build-groups-current/original-dsom-acceptance`. The actual normalized user
Server 4.52 ISO restored eighteen files: eight original SOM DLLs and ten package
companions. `initial-recovery.json` retains original source labels and hashes.
The original daemon prints `SOMDD - Ready`, creates its real named queue and
waits on its mux semaphore. A generated separate LX successfully opens and
closes that actual queue (exit 0), and another generated LX loads/frees original
SOMD (exit 0). `result.json`, `service.stderr`, `queue-probe.stderr`, and
`probe.stderr` retain the evidence. Owned test processes were terminated and
reaped. The initial sandbox-only native launch stopped with SIGILL before
logging; the authorized unsandboxed Unicorn execution produced the actual
results. A diagnostic initially assumed DosReadQueue; the real daemon instead
uses its ready message and DosWaitMuxWaitSem as the bounded checkpoint.

This establishes original package recovery, daemon readiness/queue access and
SOMD load/free. A new interactive workbook acceptance, general DSOM remote
networking, and arbitrary OLE workflows are not claimed. No actual Documents
ball or live app was changed by this work.
# File tree and explicit extraction

The shared plan now exposes fifteen stable group IDs including the full-set
selector: cmd, fonts, assets, printers, help, full, som, runtime, multimedia,
network, utilities, languages, diagnostics, legacy and opengl. The root
`plan.tree` node has idall and fourteen component branches plus Other OS/2
Files. Every eligible planned source has one file leaf with its exact source
as id. Duplicate source inputs are removed in O(nlogn); distinct source
variants remain individually selectable. Bitmap, TrueType, OpenType,
PostScript and font-metrics branches are separate; help manager/resources,
viewers, HLP books and INF references are separate. SOM libraries, services,
repositories and messages are separate.

`fileSelection` validates exact source strings against the current plan before
staging. It overrides category expansion and module-name filters, permits an
explicit empty choice, never overwrites existing files and never adds omitted
runtime libraries. Category-only clients retain their previous requirements
behavior. With multiple chosen versions, the highest source priority remains
at the usable destination and chosen lower-priority files are preserved under
MEDIA; equal-priority conflicting files retain the existing ambiguity policy.
A lower-priority source selected alone is installed as chosen. Leaves expose
`preferred` for parent/default selection without disabling manual alternatives.

File selection does not automatically provision SOM. Optional
`configureComponents:["som"]` is accepted only when every eligible SOM leaf
in the plan is selected; configuration then uses the existing essential-file
check and never imports additional files. The result reports configuredComponents,
selectionMode, automaticRequirementsApplied and dependenciesChecked. It does
not fabricate successful runtime dependency checks. A later dependency recovery
action remains independent of this explicit extraction choice.

Entirely native-provided/protected files remain in the flat plan for API
compatibility but are omitted from the selectable tree. OpenGL is an explicit
alternative: original OPENGL.DLL/PGL.DLL are eligible even when Mesa or Off is
active; copying does not switch providers, and dependency auditing's existing
provided-module policy is unchanged. OpenGL, printing and multimedia groups
expose typed provider objects and nonselectable setting nodes with stable
links graphics.opengl, printing and graphics.

Kernel-driver exclusions are bounded to canonical system/boot, MDOS,
multimedia and IBMCOM adapter/protocol driver roles. Their original .SYS,
.ADD/.IFS/.DMD/.FLT or adapter/protocol .OS2 files have no driver-loading path
in this runtime and are marked provided with an explanatory reason. COUNTRY.SYS,
CONFIG.SYS classification, code-page data, application DATA.SYS and printer
.PDR/.DRV resources are distinguished explicitly. Classification does not
hide arbitrary application files merely for sharing a suffix.

## Original-media root coverage

Read-only inspection of the remembered server ZIP → WSEB_CP2.iso found actual
network payloads outside its OS2IMAGE subtree. Bounded retained inventories
are `build-tree-current/iso-{roots,component-roots,network-roots,network-packages,network-payloads}.json`.
CID/SERVER/MPTS/MPTN contains canonical bin/dll/etc/protocol/msg directories;
CID/SERVER/MPTS/IBMCOM contains canonical dll/macs/protocol directories.
CID/SERVER/BOOKS contains INF books. Planning only those two network roots
finds384 canonical networking files in5.49 seconds, with unestablished paths
kept in Other rather than guessed as native destinations. Exact examples are
`canonical-network-plan.json`.

Automatic source opening now preserves OS2IMAGE first and adds only those
three existing exact sibling roots in the same container. Reopening a remembered
virtual OS2IMAGE source performs the same normalization. The source-browser
directory stays on OS2IMAGE and supports navigating to its parent. Direct
plan requests for an explicitly chosen folder never expand. Unverified IBMLS
and TCPAPPS installer-package destinations are not guessed or added to this
automatic selection. The base OS/2 signature source remains first for media
verification. Actual read-only normalization confirms four sources in
`expanded-normalization.json`.

Initial real-media tree qualification before the subsequent driver exclusions
planned10852 files with10811 eligible unique sources in31.97 seconds, including
utilities97, languages235, diagnostics2110, legacy3045, fonts403, help154,
SOM20 and original OpenGL1. That initial OS2IMAGE-only result correctly showed
no network files within its limited selected subtree; the sibling-root evidence
above corrected the automatic default rather than claiming those components
were absent from the CD.

Seven focused persistent tree tests cover every group/subgroup, source
uniqueness, native API omission, provider alternatives without switching,
explicit source validation/omissions, no hidden runtime additions, alternate
versions, whole-SOM configuration, ZIP→ISO sibling roots and bounded driver
exclusions. Fresh diagnostics use only build-tree-current. No user ball was
modified and no guest installer or service was launched for tree verification.
