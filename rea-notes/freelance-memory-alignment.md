# Freelance SmartHeap allocation-base failure

## Target and isolation

Observed installed release: 2ine 0.5 build 23, revision
`00534c6fdb282879efe256f46b58c36482c6209f`, ARM64 macOS / Unicorn.
The user's ball and all original binaries were read only. Probes used a COW copy
under `build-freelance-pointer-current`, a copied runtime, dummy/software SDL,
no startup programs, no desktop input, and a diagnostic constructor watchdog
that calls `_Exit(124)` after 20 seconds. The diagnostic interposes host `write`
and exits 88 at the first literal `MEM_BAD_POINTER`; it does not patch guest
bytes, skip assertions, or replace guest API results.

Original `C:\LOTUSW4\FLG\FLG.EXE` SHA-256:
`4d5a3b5ac9df801c4ec584801b0f1b313b55e27d69dd5e7c65e1c1d0cfd30cb1`.
The private original has the same hash.

## Failure provenance

The reported video failure is separate. The private original reaches the same
SmartHeap error with successful dummy video initialization. The video agent's
host interposer shows its first WinInitialize runs on a native worker thread
(`nativeMain=0`, `sdlMain=1`), followed by main-thread initialization; both succeed
with dummy SDL. Native Cocoa worker initialization remains the video agent's
scope.

The original SH30OS2 export table identifies ordinal 72 as `MemFreePtr`, object
2, offset `0x1060`. In this probe the loaded code is `0x20435060`. Its original
instructions clear BX (mask the low 16 bits of the argument), read the word at
aligned base + `0xe`, mask with `0xfff8`, and compare to `0xcad0`. The error path
calls the original SmartHeap reporter.

FLG calls this export at guest `0x39ae1`; return address `0x39ae6` and its argument
are present on the captured stack. Rejected pointer: `0x208bc764`. Its containing
runtime mapping has base `0x208bc000`, length `0x10000`, offset `0x764`. Header
magic at the actual base + `0xe` is `0xcad4` (valid after masking). Header at the
64-KiB-down address `0x208b0000` + `0xe` is zero. This establishes a valid allocated
heap pool placed at an incompatible object base, rather than a fabricated bad
pointer or evidence that video initialization caused this error.

## Allocation contract and implementation

The IBM OS/2 2.0 Control Program Programming Reference, S10G-6263-00, pages 2-6
and 2-7, specifies page-sized (4 KiB) object length rounding and separately
explains the 64-KiB OBJ_TILE alias-selector boundaries. It does not explicitly
state ordinary base alignment in that passage; it must not be quoted as doing
so. Primary implementation-client evidence is IBM's ooRexx API sample, now
maintained in the official ooRexx repository:

https://github.com/ooRexx/ooRexx/blob/master/samples/api/classic/unix/rexxapi3/rexxapi3.c

Its API discussion at lines 259–261 explicitly describes DosAllocMem allocating
on a 64K boundary. This agrees with the original unmodified SmartHeap operation
above. Reference PDF:

https://www.bitsavers.org/pdf/ibm/pc/os2/OS2_2.x/IBM_OS2_2.0_Technical_Library_1992/S10G-6263-00_OS2_2.0_Control_Program_Programming_Reference_199203.pdf

`lx_execution_map_aligned` adds an explicit power-of-two guest-address alignment
for anonymous allocations. Ordinary generic maps, exact module placements,
lengths, ownership, locking, overlap checks, and backend interfaces retain their
existing contracts. DosAllocMem requests 64-KiB bases; OBJ_TILE keeps its existing
selector-aware allocator. Shared memory already selects common addresses in
64-KiB slots, so its behavior is preserved rather than changed gratuitously.
Native identity execution uses the aligned guest candidate for its exact host
mapping. No host-address alignment is required of separate emulator backing.
The fallback for host-only calls without an execution instance is unchanged.

## Verification

- A generated real LX guest against installed build 23 fails at exit 20, the
  first ordinary allocation's 64-KiB alignment assertion (negative control).
- The fixed runtime passes `runtime_execution`, `allocation_alignment`,
  `shared_memory_apis`, and `source_boundaries` (4/4).
- Generated guests test ordinary private, OBJ_TILE private, and anonymous shared
  allocation sizes 1, 4095, 4096, 4097, 65535, 65536, 65537: aligned bases,
  exact page-rounded DosQueryMem lengths, independently writable first/last
  bytes, no cross-allocation corruption, and successful frees.
- Runtime descriptor tests cover a collision with occupied memory, preservation
  of its sentinel, invalid alignment rejection, unchanged 4-KiB mapping lengths,
  and exact module mapping at an address that is not 64-KiB aligned.
- Existing shared tests cover independent processes, common address attachment,
  grants, and reattachment. No additional shared allocator policy is introduced.
- Ruff, ty and pinned clang-format 21.1.8 checks pass for changed sources/tests.
- Native i386 Linux execution and Windows are not available locally; the same
  runtime test source covers native execution when built there.

Original fixed-runtime probe: watchdog exit 124, zero `MEM_BAD_POINTER`, zero
video failures, 781 trace lines. It passes the former failure and reaches
CreateWindowEx for the main Freelance window. The next observed gap is a
CW_USEDEFAULT width with a non-default height (2147483638), which Open32 maps
into a huge PM window. Root owns that independent window-size fix. This is
startup progress, not acceptance of a complete interactive Freelance session.

Artifacts are private/ignored under `build-freelance-pointer-current`:
`before-alignment/`, `after-alignment/`, `result.json`, `stderr.log`, `probe.py`,
`trace-write.c`, captured relocated object dumps and rejected heap mapping.
Allocator build: `build-freelance-memory-offline`.
Logs: `/tmp/freelance-alignment-baseline.log`,
`/tmp/freelance-memory-tests.log`, `/tmp/freelance-memory-build.log`,
`/tmp/freelance-alignment-original.log`.

## Coherent follow-up startup gates

Using the complete coherent runtime supplied by root at
`/Users/jimd/src/os2/tools/2ine/build-ui-release/runtime`, the next probe also
includes the independent Open32 width-default fix and #729 class-alias export.
All runtime modules were copied together into the disposable Runtime directory.
The obsolete video-init diagnostic hook was removed after its evidence directory
was renamed; the earlier retained successful initialization evidence above is
unchanged. The write/20-second watchdog diagnostic remains.

Freelance now reaches 22,387 trace lines and exits with the loader's missing
import diagnostic for `PMWINX:ordinal #600`, after LoadIcon/FindResource calls.
There is no MEM_BAD_POINTER or video initialization failure. IBM Toolkit 4.5
`lib/pmwinx.lib` OMF IMPDEF records identify ordinal 600 as `SetClassLong`.
Artifacts: `build-freelance-pointer-current/unified-next-gap/`.

A separate disposable COW clone launches the original configured Approach
application with its real working directory, no startup programs or user-ball
writes. Approach SHA-256:
`142d797c3b8bc5de5f653bdfdf488bad7ccfee1ce51cd7100bf64a846592b06d`.
It reaches 46,661 trace lines, with the actual call
`RegisterClassAlias('LOTUS_HLISTBOX_01', 'LISTBOX')`, then loads LTPLR30 and stops
at `PMWINX:ordinal #692`. The same primary import library identifies that export
as `TlsAlloc`. The loader rejects an unresolved import before that API executes;
no invented argument trace is claimed. Artifacts: `build-approach-pointer-current/`.
Both are bounded startup gates, not interactive application acceptance. Root owns
these next Open32 gaps; no additional PM/Open32 implementation was changed here.
