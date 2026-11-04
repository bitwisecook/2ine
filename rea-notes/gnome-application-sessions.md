# GTK application sessions and owned startup services

The GTK manager now retains separate session IDs, main Gio subprocess handles,
plans, logs, exit codes, stop state and startup-service leases. Ordinary
applications can run together within a ball. An installer excludes every other
session in that ball; a launch in another ball remains independent. One launch
preparation runs at a time per ball. Preparing/cancelling/finishing sessions
remain reserved until their corresponding work completes.

Run forms and application-row Launch actions use the same owned execution
path. A second same-ball launch passes `resolve: false` to the shared prepare
request, so dependency recovery cannot alter files under an existing peer.
Configuration/package mutations are guarded for the affected ball, while
sidebar and read navigation stay usable. The old `busy` property remains
selected-ball activity telemetry for existing callers; it no longer disables
the entire window merely because an ordinary guest is running.

The Applications list shows per-launcher active-session counts. Logs contains
a native selector with stable session numbers, separate output paths and exit
state. Stop/Force Stop capture the chosen session object before displaying a
confirmation, so a later selection change cannot redirect the signal. A
finishing process receives no additional signal. Changing balls restores that
ball's selected/most recent session. Snapshot re-renders do not overwrite a
separate dependency-audit report with an old application status.

Prepared startup programs were previously ignored by the GTK launch path.
They now start through a process-wide pool before the main application. Its
logical key is canonical ball path plus the prepared `program` provenance;
legacy plans fall back to their command identity. Reuse requires an identical
prepared command, cwd, environment and unset-environment set. An active
logical service with different settings is rejected rather than duplicated.
The core's ball and IPC namespace are unchanged; a session ID is never added
to the OS/2 IPC namespace.

The pool references only child handles it spawned. It releases a child's last
lease after the final application exit or failed main spawn; services still
leased by another session stay running. Supervised children receive the
runtime's SIGUSR2 cleanup request, and ordinary owned children are force
stopped. No service is discovered, adopted or killed by executable name or an
external PID. A dead child with active clients rejects a new launch and asks
the user to close affected apps before restarting. Last-release cleanup blocks
a rapid retry until the old owned child has exited. Startup spawning is not a
service readiness or original DSOM interoperability qualification.

The pool is confined to GTK's main context, which serializes acquisition and
release callbacks. Separate manager processes/external OS/2 daemons are not
coordinated by this pool. Parallel launching does not establish DDE/OLE or
other application-specific interoperation; the shared runtime owns those API
contracts. Closing the manager is declined while sessions are active, so the
manager does not silently abandon them.

## Verification

`tests/tools/test_gnome_sessions.py` covers ordinary-app concurrency,
per-ball installer exclusion, captured session state, exact-plan reuse and
mismatch rejection, dead shared children, last-service cleanup, partial
startup failure, and a real host child-process test. In that test two leases
share one owned subprocess; releasing the first leaves it alive, releasing
the last stops it, and an unrelated process running the exact same command
remains alive. All test children are explicitly reaped.

`tests/integration/gnome_sessions.py --build BUILD` is the native Linux gate.
It generates two sleeping LX applications and an independent startup service
in temporary balls, starts both apps, checks one shared child/two leases,
navigates to another ball, rejects a same-ball installer, changes selection
while a Stop confirmation is open, verifies only the captured session exits,
and checks final owned-service cleanup and separate logs. It is authored and
type/compile checked but was not run on this macOS host: GI/PyGObject is
unavailable. No user ball, user preferences or active guest was touched while
implementing these changes. Root owns unified native qualification/release.
