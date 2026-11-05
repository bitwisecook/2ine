<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# 1-2-3 welcome tabs, window sizing and shutdown

The reported 0.4(18) welcome dialog compressed its first tab to about 22 pixels,
then drew the complete label outside that rectangle. The second label overlapped
it. This was a failed measurement, not a font-width or text-clipping discrepancy.

In an isolated copy of the supplied Lotus installation, an Open32 screen DC was
live as HPS 16. Lotus then acquired a native screen PS, which 2ine assigned 17.
Bounded disassembly of that native caller showed `and al,0xfe` immediately after
the screen-PS call; it used the masked value for its capability query and release.
Consequently `WinReleasePS(16)` destroyed the live Open32 context. The subsequent
`GetTextExtentPoint32` for "Open an Existing Workbook" failed. Another acquisition
made the second tab's measurement succeed, explaining the asymmetric layout.
These are emulator trace and disassembly observations, not a stock-Warp handle
allocation measurement or REA provider Evidence.

Presentation spaces now have even handles, and lookup rejects odd handles.
The generated Open32 text contract recreates a native screen-PS acquisition and
masked release while an Open32 DC remains live, then measures text through that
DC. The host PM contract also checks distinct nested spaces and invalid odd
handles. A new real-application capture shows both complete tab labels in their
own tabs.

The main application requests Windows style `0x03cf0000`, including a thick
frame and minimise/maximise capabilities. `CreateWindowEx` previously translated
only its system-menu frame flag. It now also creates the PM size border and
min/max controls, and formats the client after creation. This enables the
existing host resizability path and preserves resizing notifications to the
Windows client. The generated sizing contract checks resize delivery,
maximise/restore, and a fixed dialog as a counterexample.

An isolated real-1-2-3 probe also reported the main SDL window resizable. Resizing
its canvas from 640×480 to 800×600 and then 1000×700 persisted and expanded the
sheet from eight columns and 21 rows to 13 columns and 34 rows. The status bar
and scrollbars followed the new bounds. This used injected SDL window resizing,
not physical mouse dragging.

Private Lotus binaries, copied profiles, runtime logs and temporary injection
helpers are not included. The captures were taken with SDL's dummy driver;
physical macOS window-manager interactions require a separate check.

## Host Quit routing

SDL's event queue is shared by the process. Previously a worker polling the
global Quit event posted `WM_QUIT` only to its own PM queue. A focused two-thread
reproduction received Quit on the worker while the main window's queue remained
empty. Quit now reaches each distinct window-owning PM queue. If a temporary
window used to identify that queue closes before delivery, the queued Quit
transfers to a surviving window on the same owner. The PM thread contract
injects SDL Quit through the real polling path and checks both queues receive
exactly one Quit after that temporary window is destroyed.

## File Exit and filtered paint

Lotus defers its Exit command into its own task queue. The investigation found
two message-retrieval problems: frame commands could execute synchronously
inside Open32 `GetMessage`, and a subsequent `PeekMessage` restricted to
`WM_QUIT` could loop forever on an unrelated invalid window. The latter kept
converting the same native `WM_PAINT` into an already-pending Windows paint
message, without ever returning false for the quit-only query.

Frame commands now return through the Windows loop before dispatch. Internal
PM retrieval accepts a paint predicate so Open32 can retain pending paints
without synthesizing duplicates or offering paints excluded by the current
query. Public native PM retrieval keeps its existing behavior. The regression
checks quit-only peeks, different window filters, retained damage, and a blocking
timer retrieval behind deferred paint.

Open32 tick counts also use the same clock as PM message timestamps and Windows
timers. Applications compare these values; see Microsoft's
[Windows time contract](https://learn.microsoft.com/windows/win32/sysinfo/windows-time).

In isolated tests with the original copied settings, selecting File > Exit and
closing the host window post Windows `WM_QUIT` and finish naturally with return
code 1, without the harness stopping the process or a CPU fault. A subsequent
cold restart with saved window placement also processes Exit, but reaches the
previously documented guest SOM-related teardown fault at `20D41451` (null
pointer read, return code -6). The quit-message hang is fixed; teardown remains
intermittent. The stock application's normal return code is unverified.

## Restoring saved placement

Successful Exit writes `DesktopWindowPlacement`, exposing a previously missing
Open32 ordinal 661 on the next startup. `SetWindowPlacement` now restores the
44-byte placement record produced by `GetWindowPlacement`, including normal,
minimised and maximised frame geometry, visibility, and MDI normal/maximised
geometry. Malformed records fail before changing the window. Explicit minimum
position and restore-to-maximised flags remain unsupported and return error 50.
Regression cases cover mapped frames, native PM frames, MDI restore rectangles,
and invalid record lengths, pointers and coordinates. The real application
cold-restarts with its saved record and reaches its Welcome dialog and workbook.
