# Installer worker lifetime (2026-11-04)

The reported Lotus install receipt covered only `CMD.EXE /C CALL D:\INSTALL.CMD`:
the wrapper returned zero while its GUI installer continued. The audit therefore
observed eleven support binaries before the installer subsequently produced
hundreds of Lotus binaries. This is a host operation boundary defect; an OS/2
asynchronous session must remain asynchronous from its guest parent's perspective.

## Implemented ownership

Supported POSIX workers advertise `operationSupervisor: true` in
`runtime-capabilities.json`. An installer launch can use
`lx_loader --operation <guest> ...`. Normal application launches retain the
existing command syntax. Missing guest arguments still produce usage before CPU
initialization; malformed inherited operation channels fail before guest code.

The dedicated supervisor starts the root worker in a separate process group and
retains two private pipes:

* `TWOINE_OPERATION_LEASE_FD` names the worker's write end. Each guest child
  inherits this lease through the trusted host spawn adapter, including children
  launched with a custom OS/2 environment. EOF at the supervisor means every
  scoped process has exited. There is no time or filesystem quiet-period guess.
* `TWOINE_OPERATION_OWNER_FD` names the worker's read end. The supervisor alone
  retains its writer. A detached host watcher in each scoped worker waits for
  owner EOF and kills that worker if the supervisor crashes or is killed.

Worker startup marks both channels close-on-exec. The POSIX spawn adapter uses
temporary close-on-exec duplicates and explicit `posix_spawn` file actions to
inherit only these operation channels into another guest worker. It does not
temporarily clear flags on shared parent descriptors. Existing ordinary guest
handle inheritance and `DosExecPgm` result/wait behavior are unchanged.

The supervisor retains the unreaped root child until lease EOF. This also
reserves the root's PID/process-group identifier while cancellation can target
the owned group. `SIGTERM`/`SIGINT` request ordinary group cancellation;
`SIGUSR2` requests `SIGKILL` for the group. The supervisor waits for EOF before
reporting cancellation and preserves the requested signal, including when the
wrapper had already returned zero. Otherwise it preserves the root worker's
exit result/signal. The extra `signal` member in the host-only
`LxHostChildStatus` records POSIX signal termination without changing guest
`RESULTCODES` normalization. All native modules were rebuilt together.

Explicit startup services are launched independently by the frontend. The
known runtime REXX system-initialization helper uses
`lx_host_child_spawn_service`, which removes both operation environment keys,
inherits no close-on-exec channels, and starts a separate process group.
Unrelated guest asynchronous/background processes are still owned: there is no
executable-name heuristic that silently exempts arbitrary guest children.

The frontend must remove the two reserved channel keys before independent
launches and use `SIGUSR2` for force-stop of a plan marked
`operationSupervised: true`. A direct external `SIGKILL` still invokes the
owner-death cleanup mechanism, but a killed supervisor cannot itself wait to
report that cleanup. Frontend/audit integration is implemented by the parent
task in the shared command planner and receiving applications.

## Verification

Fresh diagnostics are under `build-install-lifecycle-current/`; no live user
ball, installer, or desktop input was modified.

`host_operation_lifecycle` runs real host wrapper/middle/grandchild processes
with named-pipe gates and lifetime observers. It proves:

* the wrapper has actually exited while the operation remains live;
* the grandchild writes an installed executable only after the explicit gate;
* completion preserves root result 23;
* normal cancellation, forced cancellation of a worker ignoring TERM, and
  supervisor `SIGKILL` remove scoped workers;
* excluded services remain alive independently, then exit on their own gate;
* cancellation of one operation leaves a parallel operation running.

`installer_operation_apis` executes generated USE32 LX images through the real
loader/backend. Its wrapper calls `DosStartSession`, the session calls
`DosExecPgm(EXEC_BACKGROUND)`, and the grandchild blocks on a FIFO before
writing `installed.exe`. Both child launches supply an explicit guest
environment. Actual wrapper FIFO EOF precedes operation completion; the
supervisor completes only after the grandchild exits and its complete file is
visible. No proprietary guest bytes or patched original application are used.

Commands/results:

```
cmake -S . -B build-image-app-runtime
cmake --build build-image-app-runtime -j6
ctest --test-dir build-image-app-runtime --output-on-failure
```

All **68 runtime tests pass**, 38.40 seconds, in
`all-runtime-tests.log`. This includes unchanged legacy session/session API,
REXX service, startup, and host process regressions. An additional final HAL
fixture cleanup-only refinement passed its focused rerun in
`fixture-final-tests.log`. Ruff formatting/lint, Ty on both new Python harnesses,
ClangFormat on touched C/header files, and `git diff --check` pass. The first
sandboxed guest run produced SIGILL under executable-memory restrictions; the
authorized runtime run passed, as did the full suite.

## Limits and portability

This verifies operation lifetime rather than Lotus installation completion or
runtime compatibility of installed files. The actual active user install was
not restarted for this test.

The implementation is macOS/Linux POSIX. The project currently rejects a full
Windows host build; its metadata does not advertise operation supervision and
the loader explicitly rejects that mode there. The Windows spawn adapter keeps
its ordinary path and a separate trusted service path, without pretending that
POSIX descriptor numbers provide Windows ownership. A complete Windows port
needs an operation Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, an
explicit inherited handle list, and completion notification before enabling
this capability. The equivalent boundary is documented by Microsoft's
[Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)
and [Job Object limits](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_extended_limit_information).

POSIX channel inheritance follows
[`posix_spawn` file actions](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawn.html)
and pipe EOF ownership follows
[`pipe`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pipe.html).
This adds host installer ownership; it makes no reconstructed-kernel milestone
claim and does not change OS/2 session/detach contracts.

## Receiving frontend integration

The shared ball planner now opts installers into supervision only when the
receiving worker advertises the capability. Ordinary applications and startup
services retain direct launch commands; both reserved operation environment
keys are removed from independent launches. Swift and GTK force-stop select
SIGUSR2 for supervised plans. Post-install receipts pass the receiving build
through to dependency auditing. A persistent planner regression covers legacy
workers, supervised installers, direct application/service plans and the two
reserved channels; the native lifecycle fixtures exercise actual descendants.

After integrating the clipboard ABI and component tree, the coherent runtime
build passes all 72 CTest checks in 44.61 seconds; the final complete tooling
suite passes 228 tests in 61.45 seconds. Logs are
`/tmp/2ine-current-runtime-tests.log` and `/tmp/2ine-current-tools-final.log`.
Global pinned ClangFormat, Ruff lint/format, ty, export generation and diff
checks pass. These automated fixtures do not constitute a fresh interactive
Lotus installation or desktop clipboard interchange acceptance.
