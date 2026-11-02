# CMD legacy session and queue imports

Scope: 2ine tooling compatibility for the unchanged Warp 4.52 CMD.EXE, rather
than original kernel source reconstruction. This work does not pass an OS/2
reconstruction milestone.

## ABI authority

The selected original CMD target and its import inventory are recorded in
`installer-support.md`. Original declarations/layouts were checked in
`/Users/jimd/src/os2/ddk/components-2004/combase/DDK/base/h/bsedos16.h`,
lines 1573–1610 and 1681–1781. They require a 16-bit queue handle, result length,
SID and PID, packed 16:16 pointers, and a 32-bit HSEM. Byte-valued USE16 Pascal
arguments consume a stack word; the native declaration uses USHORT for fWait.
The supplementary QUEUERESULT declaration in
`ddk/components-2004/inout/DDK/base/src/dev/pcmcia/flash/mtd/bsedos.h:1172`
places PID before the request word. The original Microsoft 1.2 reference also
shows the two-word structure:
https://bitsavers.computerhistory.org/pdf/ibm/pc/os2/OS2_1.x/Microsoft_-_OS2_Programmers_Reference_Volume_4_Version_1.2_1989.pdf
(search-result transcription, not a successfully retrieved local PDF).

The repository's import catalogue gives QUECALLS ordinals 1 (read), 2 (close)
and 8 (create), and SESMGR 8 (stop), 14 (set), 17 (start), 40 (32-bit stop).
Existing CMD static imports select DOSCALLS aliases 674/681/693/699/702.

## Implemented behavior

- DOSCALLS forwards those aliases and complementary close ordinal 675 to the
  owner modules.
- Queue creation/read/close translate the low-word legacy registry ID through
  the live queue registry before borrowing a full generation-checked handle.
  Invalid IDs never get interpreted as host pointers or full modern handles.
- Legacy-created queues deny ordinary 32-bit opens. Session-manager private
  mailbox opens remain available for writing termination notifications.
- Read returns PID and request as two packed words, writes only a word of
  length, and returns a far pointer. Copied cross-process/termination messages
  receive segmented storage, so the caller can address the entire payload
  through a USE16 pointer. Legacy NOWAIT may poll without a semaphore; a RAM
  semaphore can be registered for revision notification through DosSemClear.
- STARTDATA is decoded by its declared byte length. Embedded strings,
  environment block and optional error-object buffer are checked through the
  guest pointer layer. The native32 decoder also accepts explicit environments
  and optional error-object buffers. Output SID/PID remain two bytes. Related-session status
  delegates to the existing session registry.
- Stop resolves related session IDs to retained full child PIDs and requests
  host child termination. STOP_ALL affects related sessions only. Native32
  DosStopSession shares the same implementation and has an explicit bridge.
- Invalid related/background/trace/inheritance/type/reserved options report
  errors before launching. The object buffer is passed to DosExecPgm.

## Verification

`cmake --build build-image-app-runtime -j 4 --target quecalls sesmgr twoine_legacy_session_tests`
passed. `ctest --test-dir build-image-app-runtime -R '^legacy_sessions$' --output-on-failure`
passed (2026-11-02). The native check creates a legacy queue, receives an actual
copied termination-shaped mailbox message, checks result/length output
canaries and the far pointer contents, closes both handles, checks closed-ID
rejection, and exercises session argument errors. It uses a small host segment
adapter; this check does not execute CMD or STARTDATA guest code.

## Remaining boundaries

The runtime's existing DosSem family implements RAM semaphores, not original
named system semaphore handles. Queue notifications inherit that boundary.
Queue address conversion for arbitrary externally supplied queue elements,
additional legacy queue APIs and cross-process 16-bit ownership conversion
have not been established by this test.

Stop uses DosKillProcess action 0 for the retained immediate child. Related
sessions' descendant processes are not proven terminated as a tree. Host
window foreground/bond behavior and trace sessions are not implemented;
nonzero trace requests return ERROR_NOT_SUPPORTED. No full original
DosStartSession success is claimed by
the native test. Guest end-to-end checks remain the authority for CMD and the
SmartSuite workflow.

The generated guest test `tests/integration/legacy_sessions.py` passed with
`--build build-image-app-runtime --backend unicorn` outside the host sandbox
on 2026-11-02. It executes a USE16 NE parent through DOSCALLS aliases, starts
an LX child with a far-pointer STARTDATA and explicit environment, changes the
related session state, reads the termination queue's SID/status 47, verifies
word output canaries and releases the returned segment/queue. The child
checks its inherited explicit SESSION_FIXTURE variable. This establishes the
emulator ABI path for a synthetic guest; it does not alone establish all
original CMD or SmartSuite behavior.

The existing `tests/integration/queues.py` suite also passed with that backend,
covering modern queue ordering, opaque pointer payloads, notifications,
blocking, cancellation and close wakeups. Build/use of Homebrew Unicorn works
outside the sandbox: denied cache sysctl calls inside the sandbox had caused
its privileged-register fallback. No dependency source patch was needed.
