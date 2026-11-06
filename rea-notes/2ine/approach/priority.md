# Approach Open32 process-priority compatibility

## Original interface evidence

The original PMWINX provider SHA is
`07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`.
`rea-notes/lotus-priority-api.md` retains the prior byte/REA observations for
ordinal 636 at `0x976f8`–`0x97884`, including Evidence
`ev_1c44cd46af2729f9e7b241001f6a213c5baa87fd6e3c7bee57ab4958cbaac81e`.
The installed permanent module/manual cache was consulted rather than repeating
Ghidra analysis. Its source-declaration document for `os2win.h:7026` is
`3d95e17efe7267e693deab82c5b61a2fc6fb7e547affc49a91d997aaa4e58681`.
The paired getter is PMWINX ordinal 337, `GETPRIORITYCLASS`.

The original setter accepts `0x20`, `0x40`, `0x80`, `0x100`; they map to OS/2
regular, idle, regular and time-critical respectively, all at delta zero.
Normal and high therefore intentionally share a host regular scheduling level,
while retaining distinct public class values. Invalid class returns zero and
error 307; invalid process handle returns zero and error 6. The recorded ABI has
eight stack argument bytes and caller cleanup. The getter takes four bytes.

## Implementation and supported scope

`os2libs/pmwinx/priority.c` validates the current-process pseudo-handle
`UINT32_MAX`, currently the only process handle produced by Open32. Raw PIDs,
file/thread handles, zero and other integers are rejected. This does not add
OpenProcess or authority over another native process.

A narrow public HAL operation changes current-process host scheduling, with
native permission/error propagation. A monitor serializes host changes and
publication of the requested public class. The getter uses an atomic read;
failed setters retain the previous class. Successful calls preserve last-error.
Access denial maps to 5; unsupported operations to 50; other mapped native
failures retain specific errors or general failure 31.

macOS uses actual process nice 0 for regular and 19 for idle. Process-wide
realtime is unsupported before mutation, since Mach realtime operations are
thread policies. Apple's native documentation specifies process-scope nice and
permission failures. [Apple getpriority/setpriority](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/setpriority.2.html).

Linux nice is per native task. The adapter checks `/proc/self/task` before
mutation: a single task can change its real nice value; a multithread process
succeeds only when every observed task already has the requested level.
Otherwise it returns unsupported before changing any task. It does not change
only the process leader or claim a partial per-thread operation as a successful
process-wide change. Full Linux thread-creation/rollback coordination is outside
this bounded implementation. [Linux scheduling documentation](https://man7.org/linux/man-pages/man7/sched.7.html).

The future Windows adapter uses its process-priority operation for normal/idle;
realtime remains unsupported. Windows build/runtime integration is not qualified
by these macOS tests. `DosSetPriority` is unchanged: its current implementation
only changes the calling guest thread's TIB and is not an equivalent process-wide
host operation.

## Verification

All disposable output used `build/lotus/approach-priority`, linking existing
`build/runtime` dependencies. No user Lotus files or balls were changed or run.

- `test_open32_priority.c` compiled with warnings as errors and passed 112 host
  operation checks, invalid handle/class ordering, all four original mappings,
  permission/unsupported/native error preservation, public-state retention and
  four concurrent callers. The injected host operation verifies serialization.
- Its real-host variant ran in an owned child: nice `0 -> 0 -> 19`; normal
  promotion was genuinely denied with error 5, realtime returned 50 without
  changing nice or public idle state, and the parent native priority was
  unchanged. The sandbox denies even a no-change scheduling request; the actual
  owned-child scheduling check passed with the approved sandbox escalation.
- `test_process_priority_linux.c` exercises the real Linux provider's task
  preflight with injected native task APIs: multithread rejection before any
  set operation, unchanged no-op, single-task scheduling, invalid/realtime
  validation, traversal failures and native permission forwarding passed.
  This is a policy fixture, not a Linux runtime scheduling qualification.
- C formatting, architecture ownership, Ruff and ty checks passed for owned
  changes. `open32_priority.py` is a maintained synthetic LX ordinal/ABI test;
  it awaits the parent-owned export registration and current runtime build.

This closes the isolated priority implementation work. It does not establish
that Approach has passed its remaining startup or application behavior gates.

## Parent-owned registrations

No shared generated interface was edited by this task. Required integration:

```c
OS2EXPORT BOOL OS2API SetPriorityClass(ULONG process, ULONG priority)
    OS2APIINFO(ord = 636, name = SETPRIORITYCLASS);
OS2EXPORT ULONG OS2API GetPriorityClass(ULONG process)
    OS2APIINFO(ord = 337, name = GETPRIORITYCLASS);
```

Add `pmwinx/priority.c` to the PMWINX source list. API32 entries use
`LX_ABI_STACK32`, callee_pop zero, argument_bytes 8 and 4 respectively; regenerate
bridges/exports after coordinating other ongoing interface edits.

The unit fixture can compile `test_open32_priority.c` and `priority.c` against
2ine, without linking PMWINX: its default host operation is injected. Define
`TWOINE_PRIORITY_REAL_HOST=1` for the owned-child real-host variant against the
new HAL. The Linux policy fixture compiles `hal/linux/host_process.c` with
`opendir/readdir/closedir/getpriority/setpriority` renamed to their
`priority_test_` counterparts. Register the synthetic LX test with the chosen
build/backend and a work directory beneath that build. Add its new Python path
to the global Ruff/ty lists when registering it.
