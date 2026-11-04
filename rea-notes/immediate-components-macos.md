# Immediate OS/2 component checkboxes on macOS

OS/2 Components checkboxes now describe desired installed sources and apply immediately.
The Install Selected / Remove Selected commit buttons were removed. The existing native
outline, source browser, provider links, cancellation and per-operation log controls remain.

## Ownership and lifecycle

Each AppModel retains one observable `ComponentReconciler` per ball. Its desired and
verified actual source sets survive navigation, while one serialized drain submits component
jobs for that ball. Further ticks remain usable during a job. Repeated edits coalesce into
the next difference; a source can be unticked while it installs, then safely removed after
that install completes. Installs submit the full desired selection so whole-SOM provisioning
still receives its required complete selection; activity highlights only changed sources.

Current and queued files and their ancestor rows show activity. Protected, modified and
unowned present files remain checked with disabled file checkboxes and explicit statuses.
Unticking a parent can therefore leave a partial selection and a preservation notice.
Only exact unchanged owned sources are eligible for removal. Removal sends the source/path
`selection` contract, validated under the core operation lock against ownership records;
it does not reopen or require the original ISO and has no path-only fallback.

Authoritative state is read after every completed, failed or cancelled job. Per-source edit
revisions distinguish an interrupted request from choices made while it was running.
Interrupted unchanged requests snap to actual state; later choices survive and may form a
new job. Polling does not automatically retry failed intent. State-read failure stops with
an inline verification error. Polls started before a job transition cannot overwrite its
newer verified state. Edit history is cleared only after the verified queue settles, so
later external changes are reflected rather than leaving an idle queued checkbox.

The pending-launch hook runs after the complete verified queue becomes quiescent, including
a cancellation/failure reconciliation. Component operation guards include the reconciliation
and verification intervals, not just a worker's running flag. Existing direct
`startComponentOperation` callers retain their API and background behavior.

## Defaults and persisted choices

For a first-ever component operation, available CMD/REXX support is selected by the request's
CMD default; optional fonts and other groups are not broadened by installed categories or
recommendations. Actual installed sources are always reflected. Selecting navigation is an
explicit intent, while advice-only navigation remains a highlight without selection.

The existing persisted `state.progress` marker distinguishes a new ball from one with prior
component choices. A new controller with operation history starts from verified actual state
rather than adding defaults again. This preserves an intentional CMD untick and prevents
cancelled/failed defaults from retrying after restarting the manager. A new explicit checkbox
choice, selecting navigation even if it matches initial ticks, or an explicit same-source
draft can authorize a change. No preferences or core schema was added for this policy.

## Verification

`ComponentReconcilerTests.swift` exercises first-run defaults, queued/coalesced changes,
serialized exact removal, cancellation with later and same-source edits, no retry from
polling, stale-poll rejection, protected/modified/unowned state, retry intent, changed media,
and quiescent external changes. An actual no-window NSOutlineView checkbox starts a job;
a second checkbox remains usable while the first job is suspended. Native row metadata
verifies current and queued activity with enabled mutable checks.

Generated-file integration tests use private temporary balls and media. They install files,
switch to another ball, detach the original source and uninstall an exact owned source.
A file changed after the last state read is preserved by removal, and its checkbox returns
to checked state. Another test installs generated CMD support, unticks/uninstalls it, creates
a new AppModel/controller and verifies persisted operation history prevents reinstall.
No generated executable is launched and no actual user ball or desktop input is used.

Final command:

```
swift test --package-path macos --scratch-path build-os2-component-tree-swift
```

Passed **100/100**: 61 app/model and 39 BallCore tests. The focused history/default run passed
4/4, and the broader component run passed 14 model plus 2 core tests before the last history
regressions were added. `git diff --check` passed.

A prior parallel full run hit the existing four-second live-CONFIG.SYS watcher deadline.
Its isolated rerun passed in 0.111 s, and subsequent full gates passed, including the final
100-test run. The failed run is retained rather than treated as successful. Earlier root
navigation-width and archive-page expectations were corrected by their owner before the
passing gates. The final build reported the separately owned core endpoint's `keep(c,index)`
pointer-type warning; it was reported to the core owner for correction.

Artifacts: `build-immediate-components-current/` contains `full-swift-final.log`,
`full-swift-initial.log`, `focused-tests.log`, `history-tests.log`, `watcher-isolated.log`
and `settled-negative.log`. The settled-poll test failed before clearing completed edit
revisions, then passed in the final full gate.

This is macOS frontend qualification; GTK's corresponding controller is maintained separately.
