# Concurrent application sessions

Ordinary applications can run concurrently in one ball. Each macOS launch has an independent process runner, session ID, output tail, complete log and Stop target. An installer excludes all other sessions in that ball in both directions; applications in other balls can continue. This is launch and process-lifetime support, not a claim of complete Lotus OLE/DDE interoperability.

## Manager contract and ownership

`BallBackend.execute(..., sessionID: String = UUID().uuidString, update:)` returns `ProcessResult` with that `sessionID`, its main `log`, and shared `startupLogs`. IDs contain 1–64 ASCII letters, digits, hyphens or underscores. `stop(sessionID:force:)` targets one session and also retains a stop requested before its worker starts. Legacy `stop(force:)` works only when this backend owns exactly one session; an ambiguous call never stops every app. `activeSessionIDs(ball:)` returns manager-process reservations, including preparation and cleanup.

`LaunchCoordinator` serializes C launch preparation and enforces installer reservations across `BallBackend` instances in the same manager process. A second same-ball preparation sets `resolve: false`. The C `prepare-launch` branch then performs a fresh audit without recovery: it does not copy missing original DLLs, configure search paths or provision a daemon. Audit reports and the pinned native runtime cache remain ordinary launch metadata outputs. First-launch preflight may recover required files before applications start.

Ball identity uses physical `realpath`, matching the C engine's `TWOINE_BALL`. Every app retains `TWOINE_IPC_DIR=<ball>/ipc`; session IDs do not split this namespace. No session data is stored in the portable manifest.

Startup helpers use a shared lease pool keyed by canonical ball and original startup program. Identical prepared command, working directory, environment and owner reuse one child. A changed configuration fails clearly instead of starting another daemon in the same namespace. A dead shared helper is also reported before granting another lease. Startup acceptance is not a generic service-readiness protocol: applications still perform their normal OS/2 queue/service handshake.

Each pool entry records only a `Process` that this pool actually launched. It never discovers, adopts, or terminates a PID by executable name, a manifest PID, or an external daemon's presence. Manual startup records can create pool-owned children; they do not confer ownership of an already-running manual daemon. Last-lease cleanup sends TERM, allows two seconds, then uses the existing supervisor's USR2 cancellation when available, allows five seconds, and finally uses KILL if necessary. Cleanup waits for actual exit before releasing its reservation. Prepared startup items now include `program` and optional `owner`, and use `--operation` when the receiving runtime advertises its operation supervisor, keeping descendant cleanup with that owned helper.

The shared pool and installer registry are local to one manager process. Separate macOS/GNOME/CLI manager processes do not yet share this lease accounting or installer exclusion. Externally started SOMDD discovery/reuse is not qualified. These limits must not be represented as a system-wide daemon broker.

The process runner retains ownership after its observer task is cancelled. Polling avoids spinning on a cancelled task's sleep; explicit Stop remains the termination channel.

## Interoperability inventory

| Mechanism | Current evidence and limit |
| --- | --- |
| Shared files | Apps use the same mapped C: files. `DosSetFileLocks` routes byte ranges through platform adapters; Linux and macOS have separate implementations. A shared directory alone does not prove every OS/2 share-deny/open flag. |
| Named queues | `quecalls.c` hashes ball identity plus case-folded queue name and uses `host_mailbox.c` file locks/lifetime locks. Queue packet addresses remain opaque OS/2 pointer values; pointed-to bytes need appropriate shared memory. |
| Shared memory | `doscalls/memory.c` uses ball-scoped names, process-peer grants, and fixed guest linear addresses. POSIX shared backing and lifetime locks are in `host_shared.c`; peer identity includes ball/IPC/C-drive scope in `runtime/core/process.c`. The new two-LX test directly proves one named mapping survives the creator app's exit while its peer retains it. |
| Events and mutexes | Named events and mutexes use ball-scoped shared state and host synchronization (`doscalls/semaphores.c`, `host_sync.c`). This does not qualify every uncommon semaphore mode. |
| Native PM asynchronous messages | `WinPostMsg` has cross-process transport in `pmwin/thread_messages.c`, with ball-local mailboxes, live HWND bitmaps, and 32-bit scalar MPARAM fields. Synchronous `WinSendMsg` remains local. Open32 remote posting explicitly rejects foreign window prefixes. |
| Clipboard | Public text/RTF/HTML/image representations use the shared host clipboard adapter. OS/2 private formats and local atom/owner metadata are not a general cross-process binary clipboard transport. Existing fixture tests qualify conversion/lifetime behavior; this work does not read the user's clipboard or claim a new desktop paste acceptance. |
| DDE | Open32 `dde.c` implements per-thread instances, strings and limited service-name bookkeeping. It explicitly rejects peer registration notifications requiring transport with `DMLERR_DLL_USAGE`. Native `WinDdeInitiate/Respond/PostMsg` are present in the original import catalogue but have no current implementation/export. Conversation/connect/client-transaction transport is absent. |
| SOM/DSOM and OLE | Original user-provided SOM DLLs and SOMDD can use available queues/shared memory/sockets. Shared daemon lifetime is now managed correctly within one manager process. The new tests do not prove Lotus embedding/linking, remote DSOM, or a complete OLE framework. |
| Pipes | Anonymous/inherited pipes have a native adapter. `DosWaitNPipe` explicitly notes that no named-pipe provider exists. |
| Profiles | The initial audit found cached snapshots could overwrite peer keys. The subsequent [profile transaction correction](profile-concurrency.md) adds cross-process locking, refresh-under-lock writes and identity-based stale-read refresh for host-backed profiles, including the normal ball USER/SYSTEM INIs. Custom profiles inside filesystem-image members and non-cooperating external writers remain outside that guarantee. Separate-process regressions cover independent keys, deletions, binary data and lock-owner death. |

## Verification

All new tests use generated executables, disposable balls and owned helper processes. No user Documents ball, proprietary application, desktop input or clipboard data is touched.

- Full BallCore suite passed 37 tests before the final test-only observer-cancellation case was added: `/tmp/2ine-multisession-core-final.log`.
- The observer cancellation test and both installer exclusion directions then passed: `/tmp/2ine-multisession-last-tests.log`.
- Core no-recovery/startup-metadata and freshness tests passed 8/8: `/tmp/2ine-session-c-tests2.log`.
- The real portable runtime test passed with two generated LX apps: `/tmp/2ine-multisession-real-lx.log` (3.596 seconds). Both had distinct session logs; they exchanged named shared-memory words; one was stopped; the other verified its mapping after peer exit and remained alive until its own Stop. Immutable, distinct audit records show first recovery enabled and second disabled.
- Controlled host-runtime cases prove one shared startup PID across backend instances, independent stop/logs, survival of an unrelated same-name host process, last-user cleanup, startup configuration mismatch refusal and Stop before worker startup.

Reproduce the real LX gate (the optional gate is explicitly skipped unless the runtime is provided):

```sh
TWOINE_TEST_RUNTIME=$PWD/build-image-app-runtime \
SDL_VIDEO_DRIVER=dummy SDL_RENDER_DRIVER=software \
CLANG_MODULE_CACHE_PATH=$PWD/build-multisession-current/module-cache \
SWIFTPM_MODULECACHE_OVERRIDE=$PWD/build-multisession-current/module-cache \
  swift test --disable-sandbox --package-path macos \
    --scratch-path build-multisession-current/swift \
    --filter independentLXAppsExchangeSharedMemoryInOneBall
```

The controlled LX fixtures are generated by `tests/integration/session_guests.py`; ordinary lifecycle cases are in `macos/Tests/BallCoreTests/LaunchSessionTests.swift`. Run the real gate outside the restricted execution sandbox when Unicorn's memory/execution backend requires it. This is not a reason to alter system settings or use user data.
