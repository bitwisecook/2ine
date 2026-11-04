# Cached launch dependency status

Public domain. No rights reserved. Wholly AI-authored.

The user reported Word Pro being redirected to OS/2 Components because the full installation audit included optional Notes/database connectors and unrelated networking tools. The actual saved full audit had 595 binaries, 19 missing libraries, and 24 unreachable imports. These are retained as installation evidence; they do not describe Word Pro's startup requirements.

The later user requirement is authoritative: scan on installation, adding an application, component installation/removal, or an explicit **Rescan Dependencies**. Ordinary launch, preflight, snapshot, configuration editing, and refresh use saved JSON. They do not discover/recover files or inspect installation media.

`cache/dependency-index.json` schema 1 stores normalized guest paths, executable format/validity, counted imports, verified runtime companions, command-file REXX classification, package-companion status, generation time, and an unknown flag. It stores no executable bytes, host paths, or proprietary content. Full installation audits/logs remain separate. New/changed application saves, CLI add/restore-app, import-files, component import/removal, record-install, and explicit audit update the graph. Existing applications outside C: are included when an authorized scan runs. Editing arguments/environment alone does not scan.

`launch-dependency-status` accepts the existing launch arguments/environment/workingDirectories/build shape. `ensure-launch-dependencies` remains a compatibility alias. `prepare-launch` attaches its compact result as dependencyAudit. Each selected executable/interpreter and explicitly configured startup service gets a separate graph traversal under its own environment. The result is scope `launch`, cached true, scannedBinaries 0, with roots, cachedBinaries, indexGenerated, missing/unreachable/disabled dependencies and componentGroups. Direct and transitive imports remain warnings. Unreferenced optional DLLs do not enter the closure. Known component advice can be opened explicitly; cached results never force an OS/2 chooser.

Missing/unusable indexes or roots return unknown true and a nonblocking Rescan Dependencies warning. They never trigger a launch-time rescan. Existing full audit JSON is a read-only fallback, including legacy reports that contain import edges but no per-binary module arrays. Relocation resolves its historical absolute audit path back under the current ball's logs. The conversion performs one normalized path lookup per import edge rather than a binary-by-import allocation loop. It does not persist a migration during launch. The saved database itself is portable.

External binary/file changes are intentionally not rediscovered at launch. The loader remains authoritative until the next allowed mutation or explicit rescan. Other dynamic DLL loads and API/ordinal compatibility remain outside the static graph. Snapshot reports are explicitly cached/as-of the saved audit; the former recursive inventory freshness walk is no longer part of normal refresh. The legacy ensure-dependencies action is also cached-only. Full audit still checks bounded metadata stability while it is authorized to scan.

GTK exposes Rescan Dependencies on Logs, component entry points, and its existing action/menu. It calls audit-dependencies with resolve false, updates the saved graph/report, and never starts media recovery or an automatic chooser. Cached launch advice warns and continues. Native GTK execution remains unqualified on this macOS host because GI is absent; Ruff, compilation, and GI-free model/core checks passed.

Validation in the private release worktree:

- 45 dependency, cached-graph, and ball regressions passed. Nine launch/session/prerequisite regressions also passed; daemon provisioning is explicit fixture setup and prepare only reads the stored graph.
- Focused legacy conversion and read-only lookup tests passed after the final conversion optimization.
- Tests cover optional plugins, direct/transitive imports, cycles, unknown/no-index, removal of actual binaries without a query re-read, current LIBPATH re-evaluation, separate app/service environments, command/interpreter/REXX roots, no launch media recovery, add-application/component install/removal refresh, relocation/legacy fallback, and byte-identical read-only queries.
- Shared ball library builds. Pinned Clang 21 formatting, source architecture boundary, Ruff, Python compile, GI-free ty, and diff whitespace checks pass.
- Final actual Lotus Word Pro read-only query used its existing audit JSON: cached true, unknown false, complete true, 16 dependency records, zero missing/unreachable/component groups, scannedBinaries 0. Final measured query was 0.004804 seconds. Watched ball.json, CONFIG.SYS, os2-media.json, and full latest audit bytes/mtimes were unchanged; no index was created in the user's ball.
- Actual query JSON evidence: root build-ui-release/wordpro-cached-dependencies.json.

No actual user-ball mutations, proprietary application launches, commits, pushes, or installation were performed for this task.

Follow-up registration gate: prepare-launch projects existing WPS registrations before looking up the saved launcher in the cached dependency graph. Its existing prepare routine retains the same registration guard. This repairs newly registered names when logs were omitted, recreating only the WPS registration log and launcher projection; standalone launch-dependency-status remains read-only. The omitted-logs regression explicitly asserts cached/unknown/scannedBinaries=0 and absence of a dependency index or audit. All 19 WPS application, launch-session, and launch-prerequisite tests pass after this narrow ordering fix, as do Ruff/ty/format/boundary/diff checks.
