# Lotus installer: raised software exceptions

Bounded task: expose and implement DOSCALLS ordinal 356, `DosRaiseException`,
for the SmartSuite 1.7 installer. Acceptance is an executable guest regression
for actual handler dispatch and continuation, rather than import resolution alone.
This is a 2ine compatibility implementation; no OS/2 reconstruction milestone
or original-file equality is claimed.

## Authority and target

- Original `ENGLISH/INSTALL.EXE`, extracted from
  `/Users/jimd/Downloads/Lotus SmartSuite 1.7 for OS2.iso`, SHA-256
  `fdd693a7e84cdfb2dc789ffabaa6a4034e65c09dc692e7e7a4d8754702abb586`.
  Static import audit identifies DOSCALLS ordinal 356. Static imports establish
  the dependency, not that a particular exception path has executed.
- IBM OS/2 Toolkit 4.5 `h/bsexcpt.h`, SHA-256
  `bbe87dd5b7c513f461d2ff48448a644e07f0f3c261a18e86c9077cb8173c83c4`.
  This supplies the 176-byte `CONTEXTRECORD`, its context flags, 20-byte exception
  header and variable parameter sequence, handler argument order and dispositions,
  and the OS/2-specific noncontinuable/invalid-disposition codes `C0000024` and
  `C0000025`. These differ from the corresponding Windows numbers.
- Existing `DosSetExceptionHandler`, `DosUnsetExceptionHandler` and
  `DosUnwindException` provide the guest TIB chain and callback conventions.
  No decompiler prototype is used as ABI authority.

## Implementation

`DosRaiseException` validates and snapshots the guest report, including user
exception parameter sequences longer than four words, creates a guest report and
integer/control/segment context, and dispatches the current thread's registration
chain from youngest to oldest. It preserves the chain during ordinary search.
The report's exception address is the suspended caller's continuation address.
Successful ordinary continuation returns `NO_ERROR`; a handler can modify the
supplied integer and control context before continuing.

Continuing a noncontinuable exception or returning an unknown disposition raises
a noncontinuable secondary report that points to the original report. An
unhandled report terminates the guest through `DosExit`; it does not return false
success. Report and registration pointer validation rejects unmapped memory,
cycles and excessive chains. Nested raises carry the nested-call flag and are
bounded to avoid unlimited host recursion.

`lx_execution_api_resume32` defers context application until the API bridge has
finished its C callback and released borrowed buffers. Unicorn applies the
registers to the suspended invocation. Native i386 retains the restore words in
thread-local host storage before switching ESP and restoring registers, selectors,
flags and the target address. This avoids overwriting an active host C stack when
the handler selects an overlapping guest stack. Native CS and SS must remain the
active flat code and stack selectors.

Both export paths use the generated bridge, as `DosUnwindException` already does.
The function explicitly receives an encoded guest report address; native host
pointer widths are not used to decode the guest structure.

## Verification

Passed on ARM64 macOS with Unicorn:

```
cmake --build build-image-app-runtime --target doscalls lx_loader -j 8
python3 tests/integration/exceptions.py --build build-image-app-runtime --backend unicorn
/Users/jimd/.local/bin/ruff check tests/integration/exceptions.py
```

Execution uses an unsandboxed tool call because the sandbox denies Unicorn's
cache-configuration sysctl and its fallback MRS instruction raises SIGILL. No
Unicorn source or installed dependency was modified.

The regression covers existing partial/full unwind, null report rejection,
LIFO handler search, five user parameters, unchanged exception registration,
handler edits to EIP/ESP/EAX/EBX, preserved ESI, unhandled guest termination,
noncontinuable continuation escalation and invalid-disposition escalation.
C files were formatted with the repository's pinned clang-format 21.

Native Linux execution has not been run locally: the configured Docker endpoint
`~/.colima/x86_64/docker.sock` is absent. The native continuation implementation
was source reviewed but remains runtime-unverified until the same test runs with
`--backend native`. The regression is already part of CTest `exception_apis` for
both configured backends.

Limits: x87 state is not supplied (the context explicitly advertises only
CONTROL/INTEGER/SEGMENTS); hardware CPU faults and debugger-only dispositions
are not routed through this software-raise path. The actual SmartSuite root launcher subsequently exercised original REXX unwind
and reached INSTALL.EXE startup; complete installer exception coverage remains
unverified. Existing `DosUnwindException`
limitations and broader signal/exception integration are separate work.
