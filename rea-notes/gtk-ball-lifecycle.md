# GTK ball package management

The GTK manager has selected-ball main-menu actions and per-row native menus for
Rename Ball and Move Ball to Trash. Row menu actions carry the clicked package's
path explicitly; they do not depend on whichever row is currently selected.
Rename submits the shared `rename-ball` request with the new name and consumes
its returned path/snapshot. Package movement, collision guarantees and supported
internal absolute-reference rebasing remain in that shared engine.

Trash calls Gio.File.trash on the validated package directory. Only an explicit
NOT_SUPPORTED result offers a separate destructive Delete Permanently dialog.
Cancellation preserves the package. Other errors surface normally; they do not
trigger permanent deletion. Permanent deletion uses shutil.rmtree on the same
validated package, which does not traverse symlinked directories. Mapped drive
paths are not deletion targets. A package must be a real .2ineball directory with
a regular ball.json, and cannot be root/home/a package symlink. Its device/inode
is captured when opening the dialog and checked again before mutation, rejecting
a replaced package.

Actions and callbacks reject work while an operation or guest is active.
Successful rename rekeys drafts, known paths, file selections, browse state and
the active log path. Removal drops only that ball's drafts/references. Affected
undo entries and icon cache entries are discarded; unrelated ball state remains.
Preferences are persisted and the list is rebuilt from existing packages.

Five disposable-fixture helper tests verify package/root/link/collision guards,
replacement detection, extension handling, draft/log rebasing and independent
state when an unselected ball is removed. Ruff/ty, Python syntax and menu XML
checks pass. The real GTK integration harness is
`tests/integration/gnome_ball_management.py`; run under a Linux virtual display
with the newly built shared engine:

```sh
xvfb-run -a python3 tests/integration/gnome_ball_management.py --build BUILD
```

It exercises the C-backed rename, captured context target, cancellation, active
guest guard and the separate unsupported-Trash/permanent-delete confirmation.
All payloads are temporary fixtures, including an external symlink target which
must survive deleting its containing ball. The host's real Trash is not used.
GTK execution remains unqualified on the macOS development host, where gi is
unavailable. No user ball was created, restored, renamed or deleted in testing.
