# REXX cross-process named mutex

The original REXXINIT service creates `\SEM32\REXXAPI.SEM`; the client opens
that name in another process. Before this change DosOpenMutexSem searched only
its process-local registry. This is a concrete runtime prerequisite, not an
inferred need to stub another export.

Original API declarations were checked in
`ddk/components-2004/combase/DDK/base/h/bsedos.h:1278–1282`. The emulator retains
recursive mutex ownership, millisecond/infinite timeout behavior, a busy-close
error for local ownership/waiters and ownership transfer with
ERROR_SEM_OWNER_DIED (105). Named handles always select shared behavior. Names
are case folded and hashed with the existing ball/IPC scope.

The POSIX HAL uses a live-reference shared object for lifetime and a separate
advisory file lock for ownership. A recursive process-local pthread mutex
arbitrates threads sharing the local resource. Kernel file-lock release on
process termination provides process owner-death recovery; a shared nonzero
owner marker distinguishes abandonment from normal release. TLS owner records
release the file/local locks on thread exit while retaining the abandonment
marker, including recursive ownership. Cancellation is disabled while changing
ownership/TLS state and enabled during contention waits. Waits poll at one
millisecond with a monotonic deadline.

Same-process DosOpenMutexSem name aliases share the same local object, so
recursive requests through an alias retain recursion semantics. The existing
handle registry, guarded resource borrows and last-public-close retirement
remain responsible for handle lifetime. On Windows the new HAL entry uses the
native named mutex object; that backend was not executed in this macOS session.

Validation on 2026-11-02:

- `host_sync` passes with the new named-object checks: missing/duplicate names,
  initial ownership, recursion, another thread's timeout/non-owner rejection,
  cancelled named-lock wait cleanup, a thread returning with recursion held,
  owner-death transfer, last-close
  disappearance and clean recreation.
- `tests/integration/named_mutex.py --build build-image-app-runtime --backend unicorn`
  passes outside the macOS sandbox. A generated LX parent and separately
  executed child verify named cross-process opens, case folding, zero/finite
  timeout under held ownership, recursive aliases, busy close, child process
  exit while owning, parent acquisition returning 105, and final-close name
  retirement/recreation.
- Existing `mux_wait.py`, `queues.py` and `install_apis.py` generated guest
  regressions all pass with that build/backend.
- Source-boundary checks, focused Ruff checks and clang-format 21 checks pass.

This work does not implement cross-process opening of an unnamed shared mutex
by an inherited numeric HMTX, nor DosQueryMutexSem. The current named event and
named mutex backing identifiers remain separate namespaces, as in the existing
emulator. No claim is made that all original REXX service behavior completes
solely because these synthetic mutex checks pass.
