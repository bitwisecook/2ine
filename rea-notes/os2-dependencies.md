# Post-install OS/2 dependency auditing

## Missing-component advice (2026-11-04)

Both desktop frontends show compact advice in Applications and Application
Installation from the selected ball's saved dependency audit. Recognized OS/2
component groups link to their component-tree rows; the tree clears its search,
expands ancestors, highlights and scrolls to the target. Advice navigation is
focus-only: it does not tick the target or seed default installation choices.
Existing user selections are preserved. Other missing libraries remain plain
text with an audit link rather than claiming they are available OS/2 components.
Stale audits offer a refresh instead of showing outdated component links.

Exact NETAPI/NETAPI32 names now map to Networking Files; HELPMGR/HPMGRMRI map to
Full Help. SOM and runtime mappings are unchanged. The dependency metadata
fingerprint is version 3 so older saved audits are refreshed before presenting
these mappings. This is advice about static dependencies, not proof that optional
network or database integrations execute.

The focused Swift run built the UI and passed four model/audit checks, including
focus-only navigation preserving check state. The dependency/freshness/tree
Python suites passed 39 tests. Logs: `/tmp/2ine-advice-swift-final.log` and
`/tmp/2ine-advice-python.log`. A final freshness/grouping rerun passed seven tests
after the version-3 fingerprint change (`/tmp/2ine-advice-freshness-final.log`).
The wider Swift run compiled but reported two navigation assertions outside the
advice code: `allNavigationDestinationsRemainVisibleAndOrdered` (segment width)
and `importedMissingFolderCanBeReconnectedAndUndone` (starting page). Its log is
`/tmp/2ine-advice-swift-suite.log`; no full-suite pass is claimed.
GTK UI execution is not available on this macOS
host (PyGObject is absent). GTK's documented
[ColumnView.scroll_to](https://docs.gtk.org/gtk4/method.ColumnView.scroll_to.html)
retains scroll requests until an unmapped view is shown.

The 2026-11-03 request is general application support: audit after every installer
exit, resolve missing original system libraries from remembered or attached OS/2
media after a successful install, and retain all results. This work does not mark
any original-OS reconstruction milestone as achieved.

## Shared engine and policy

`ball/os2_dependencies.h` supplies the `audit-dependencies` C action. Its optional
`resolve` boolean defaults to true. `record-install` invokes the same auditor for
every validated receipt, enabling recovery only when the actual guest exit code
is zero. Audit errors live in their own report and do not replace the installer's
exit code or prevent its receipt from being saved. Native frontends consume the
compact `dependencyAudit` snapshot/receipt field, with `summary`, `logPath`,
`jsonPath`, `missing`, `imported`, `complete` and `staticOnly`.

The checked metadata reader handles OS/2 LX, LE and NE counted module import
names. It bounds file offsets, headers, table lengths and string references,
rejects unsafe module names and reports malformed metadata. PE, Windows NE,
DOS and other formats are inventoried without claiming their imports were
checked. It reads only metadata rather than executing guest code. Native
services use the shared `os2_module_provided()` list; SOM and HELPMGR are
original DLLs rather than native-provided replacements.

The auditor scans private C: recursively without following symbolic links,
excluding the native bridge cache. It follows dependencies reached through the
configured BEGINLIBPATH/LIBPATH/ENDLIBPATH and a binary's own directory, with a
seen set for cycles. An installed DLL outside those search locations is reported
as unreachable. A resolved DLL with invalid metadata or the wrong executable
platform is reported as incompatible and preserved. Per-application working
directory/LIBPATH overrides and runtime API/ordinal compatibility are outside
this static audit; these limits are in every report. A known runtime exception
includes original HELPMGR's dynamically loaded HPMGRMRI resource DLL, established
by the original-help runtime work. HELPMGR recovery also imports VIEW.EXE and
VIEWDOC.EXE when present. Unknown dynamic library loads remain undiscovered.

Recovery sources come from `os2-media.json`, explicit source arguments, or mapped
media. Attached unrelated application CDs are rejected. CMD.EXE and SYSLEVEL.OS2
signatures establish OS/2 installation media; remembered floppy sets can supply
these signatures across separate disks. Recovery indexes OS2IMAGE first, then
searches the full trusted disc for optional components still missing. Each
candidate must itself have readable OS/2 executable metadata. Per-module recovery
is best effort, so one unreadable NETAPI candidate cannot block SOM or help.
The shared selective extractor stages/validates files, respects original-source
priority and preserves all existing destinations. Recovered files record the
opaque source path and destination SHA-256.

Per-container core/full indexes are cached under `cache/dependency-media-v1-*`.
The cache binds the physical outer source to size, timestamp and SHA-256 of its
first/last 64 KiB; nested container lookup does not extract the entire outer
image. Cached member paths must remain inside that container and chosen DLLs
are parsed again before recovery. Native folder indexes remain request-scoped.
These metadata fingerprints are an optimization, not a cryptographic signature
of every byte of the complete distribution.

Every audit writes a unique `logs/dependency-audit-<time>-<sequence>/audit.json`
and human-readable `audit.txt`; `dependencies-latest.json` is a compact pointer.
Reports include media, searchable paths, all binary imports/statuses, unresolved
locations, sources and hashes of recovered files, errors and declared limits.
The configured limits are 100000 directory entries, 30000 binaries and 16
recovery closure rounds, with incomplete/error reporting if reached.

## Validation and original-media evidence

`tests/tools/test_os2_dependencies.py` contains eleven synthetic metadata tests:
checked LX/NE tables and bad offsets; import cycles/native services; unreachable
libraries; remembered split floppy media; rejection of unrelated application
media; transitive recovery and source hashes; nonclobbering; audit on successful,
failed and cancelled installer exits; full-disc secondary lookup, persistent
cache hits and malicious out-of-container cache rejection; wrong-platform media
and installed DLLs; and original-help package companion policy. Synthetic
metadata fixtures do not establish that their binaries execute.

The combined dependency, OS/2 support and ball engine run passed 40 tests in
`/tmp/2ine-dependency-tests12.log`. Ruff check/format and ty check pass for the new
Python test. Pinned clang-format 21.1.8 was applied to the C header. The final
human-report section change is checked by the focused rerun recorded in
`/tmp/2ine-dependency-tests14.log`.

The actual installed Lotus ball was copied to ignored
`build-lotus-visuals/WordPro.2ineball`; the real Documents ball was not modified.
A read-only audit scanned its 405 binaries in 0.229 seconds, finding 23 missing
library names, no unreachable imports and no parse errors. Several names belong
to optional Notes/database adapters; missing imports across every installed DLL
do not prove every application launch requires those adapters.

Using the original 4.52 Client disk-one ISO, the isolated audit then scanned
420 binaries and recovered 15 files in 94.4 seconds including core/full source
indexing: HELPMGR, HPMGRMRI, VIEW, VIEWDOC, eight SOM DLLs, LIBCS, LIBUNI and
UCONV. Every recovered file has a SHA-256 in the report. Seventeen library names
remain missing, including optional Lotus/database components and NETAPI/NETAPI32.
Four distinct GUIBASE ZIP members were unreadable and logged rather than
blocking the other repairs. The first recovery result is recorded in
`/tmp/2ine-real-dependency-recovery-new.log`, with full report at
`build-lotus-visuals/WordPro.2ineball/logs/dependency-audit-1793711011-000/audit.json`.

A fresh-process audit of that repaired copy used both persistent cache indexes
and completed in 0.421 seconds, with no additional files imported. Its exact
opaque NETAPI member errors and cache status are in
`/tmp/2ine-real-dependency-cached.log` and
`build-lotus-visuals/WordPro.2ineball/logs/dependency-audit-1793711038-000/audit.json`.
The unreadable legacy archive codec/source issue is a separate active extraction
investigation. No runtime compatibility or successful application startup is
claimed from these static audit results.
