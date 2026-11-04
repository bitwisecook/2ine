# GTK installation workflow and component reconciliation

The main navigation uses native wrapping toggle buttons with visible labels in
this order: Drives, OS/2 Components, Application Installation, Applications,
Settings, Logs. Labels remain visible when space is narrow; pages are not
moved into an overflow selector. A frontend-created new ball starts in Drives.
Reopening a ball uses the read-only `ball-navigation-state` facts: no installed
components selects Components, components without a valid saved application
selects Application Installation, and valid applications selects Applications.
Only entering a different ball/new ball applies this initial destination;
metadata refreshes preserve the current page.

The installer form has one native editable program combo plus Browse. Attached
media suggestions are read asynchronously. Ranking uses exact folder depth,
INSTALL/SETUP/INST/OS2INST/AUTORUN, EXE/CMD/BAT/COM, then English and natural
name ordering. Other supported executable/command files at the root remain
fallback choices; unrecognized nested files are not suggested. Discovery is
bounded to depth two and 96 folders. Unreadable subfolders do not discard valid
root choices. Case-insensitive duplicates are removed. A late scan preserves
manually typed, browsed or explicitly chosen text. Changing/ejecting media
clears an automatically picked old path before selecting a new default.

The selected media row says Attach Image or Change Image as appropriate, with
an adjacent Eject action. Locked devices, current media work and incompatible
ball mutations disable those actions. Ejection refreshes candidate state.
Run/Add Application retain their normal editable program rows.

The shared native browser supports expansion notifications, and GuestBrowser
uses `FolderChildren` for lazy folder disclosure. A folder can expand in place
through the native tree arrow without navigating away from its parent. Only
explicit expansion loads its children; filtering/automatic expansion during a
rebuild does not recursively read unopened folders. Empty folders lose their
placeholder arrow after the read. Failures remain inline and another explicit
expansion retries them. Existing executable filters, type-to-search, sorting,
selection and validated drops use the same renderer and domain guards.

## Immediate component choices

There is no second Install/Remove button. Checkbox changes update one desired
state per ball and serialize background work. A short coalescing interval lets
quick group/file edits combine. Current and queued source leaves plus their
ancestor directories/groups show activity. Checkboxes remain editable while
component work runs. Guest launches and other mutations of that ball are
excluded, while another ball and read navigation remain available.

Actual state is read by exact `{source,path}` selections. Imports send all
current desired sources so whole-SOM configuration still has its complete
explicit selection; activity marks only the changed subset. Removal sends the
ledger-resolved source selections and does not require the ISO or replan it.
Modified, unowned and protected installed files remain checked and disabled;
parent unchecks clamp them back. Other native-disabled source groups never
become inferred uninstall targets.

Default CMD/REXX is automatic only for a first component setup whose initial
state has no persisted operation history. With history, an ordinary reopening
starts from verified actual presence. Thus an intentional CMD uninstall does
not reinstall itself on restart. A real checkbox/selecting navigation request
marks explicit intent even if the displayed defaults were already checked.
Advice focus-only links do not select or install their highlighted group.
Existing desired drafts survive polls and navigation.

Each attempted batch records per-source edit revisions. After every result,
including a successful zero-copy result, unchanged attempted intent snaps to
fresh actual state. This prevents repeated jobs when another source owns a
preserved canonical destination. New edits made during the batch survive and
can create a fresh delta. Unedited external actual additions/removals are
observed without being treated as new delete/install requests. Partial copies
remain installed, Cancel is cooperative, and read-state failure pauses
reconciliation with an inline error instead of retrying automatically.

## Command support before launch

The frontend calls `launch-prerequisites` before preparing a runtime command.
A CMD/REXX requirement retains the original requested arguments and overrides
as a pending per-ball launch and routes to the real support group. Selecting
that group uses the same immediate installer pipeline. Once the whole queue
is quiescent, a fresh prerequisite query can resume that already requested
launch only if the ball remains selected, idle, and no application peer is
running. Deferred/non-selected requests expose Continue. Cancel Pending Launch
removes that future request; it does not stop an already running guest.
Configuration issues are shown as prerequisite issues rather than extraction
loops. No application is automatically launched merely by opening a ball or
remembered media without a prior pending user launch.

## Checks and limits

Pure model cases cover ordering/manual overrides, lazy folders, initial page
facts, defaults/history, protected files, coalesced edits, partial cancellation,
revision-aware retry, zero-progress preservation and external state observation.
The native GTK gates are updated for wrapping navigation, the editable combo,
folder disclosure and immediate defaults/unticks with an unavailable source.
They are authored/type/compile checked but not executed on this macOS host;
GI/PyGObject is unavailable.

A real shared-core + Python queue projection probe used only generated files
in a temporary ball. Default CMD selected three files and copied all three.
After removing the entire fixture media folder, a modified CMD stayed checked
and byte-identical while two unchanged support files were removed through the
no-media selection API. Reinitializing default CMD with persisted operation
history retained only the actual modified CMD and queued no reinstall.
No user ball, preferences or proprietary installer was changed/launched.
