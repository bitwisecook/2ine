# Open32 thread-local storage

## Contract and authority

The real Approach startup resolves PMWINX ordinal 692 after LTPLR30 initialization.
IBM Toolkit 4.5 `h/os2win.h` declares DWORD TlsAlloc(void), BOOL TlsFree(DWORD),
LPVOID TlsGetValue(DWORD), and BOOL TlsSetValue(DWORD,LPVOID), with
TLS_MINIMUM_AVAILABLE=64. Its `lib/pmwinx.lib` OMF import records establish the
ordinals 692, 693, 694, 695 respectively. These exports use caller-cleaned Open32
32-bit linkage, including an unmodified guest pointer-sized value. The 64-bit
host must not translate or dereference that value.

Selected original DLL:
`/Users/jimd/src/os2/extracted/install-media/os2image/FI/DAX/DLL/PMWINX.DLL`, SHA-256
`07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`.
Its original LX object 1 export offsets are TlsAlloc 0x87000, TlsFree 0x86e8c,
TlsGetValue 0x86d54, TlsSetValue 0x86bf8. Code was unpacked using the repository's
EXEPACK2 decoder without guest execution or byte modification.

Original TlsAlloc scans two 32-bit allocation masks, checking the corresponding
bit of each in order, and returns -1 on exhaustion. Original Get/Set compare the
index against 0x40 and access the per-thread value array without checking the
allocation bitmap. Free clears the bitmap bit for every in-range index, including
an already-free index. The implementation preserves this bounded behavior;
indices are opaque to applications, not guaranteed to be sequential.

REA/Ghidra confirmed the original allocation loop in retained evidence
`ev_90e814fcae69788afb13e8f418718d3114d7fb188f149ddc3b4aa716cd717e82`,
`build-open32-tls-current/tls-alloc-rea.json`. Analysis used an isolated raw export
slice at preferred address 0x97000. It confirms loop bounds and return values;
unapplied relocation operands are not treated as real runtime addresses.
SDK declarations, original opcode disassembly, unpacked slices and diagnostics
are retained privately under `build-open32-tls-current/`.

The documented Windows contract supplies zero initialization on allocation and
successful TlsGetValue clearing LastError, including a zero data value:
https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsalloc
https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsgetvalue
https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsfree
These requested contracts are implemented explicitly; no claim is made that
modern Windows' 1,088-slot expansion capacity exists in original Open32.

## Implementation

`pmwinx/tls.c` owns the process allocation bitmap and generation counters behind
a host monitor. Each host thread has fixed-size local arrays of 64 opaque guest
values and generation tags. A reallocated index increments its process generation,
so old values become zero on every existing thread, without touching other
threads' local storage. Fresh host threads start with zero arrays; thread exit
releases their local storage automatically. No heap TLS contexts or host-key
values require deferred destruction. No guest allocations are freed by TlsFree.

Invalid indices fail with ERROR_INVALID_PARAMETER. Exhaustion returns
TLS_OUT_OF_INDEXES with ERROR_NOT_ENOUGH_MEMORY. Get/Set perform the original
range-only validation. Get clears LastError on success; Set/Free preserve it.
The four API bridges copy scalar DWORDs only.

Native-module shutdown releases the monitor and clears allocation ownership.
It increments, rather than resets, all generation counters, so old per-thread
values cannot match a new allocation after shutdown/reinitialization. As with
other native-module resources, shutdown requires quiescent callers.

## Verification and actual startup gates

Focused tests pass 3/3 in 0.31 seconds:
- `open32_tls`: generated LX guest exercises all four original ordinals,
  invalid bounds, opaque values including 0xFEDCBA98, successful null/non-null
  retrieval and LastError, all 64 indices/exhaustion, original double-free
  acceptance, forced reuse when every other index is occupied, zeroing on an
  already-live worker, independent main/worker values, and fresh worker cleanup.
- `open32_tls_cleanup`: native test calls actual `lxNativeModuleDeinit`, then
  allocates all 64 indices again and checks every value is zero. Two full cycles
  verify both allocation reset and preserved generation invalidation.
- `source_boundaries`.

Pinned clang-format 21.1.8, Ruff and ty pass for the changed sources/tests.
Native i386 execution is not available on this ARM64 host; the generated test
accepts the native backend for that platform's corresponding CI run.

After copying the coherent module into the disposable runtime, unmodified
Approach executes TlsAlloc() and TlsGetValue(0), then stops at missing PMWINX #258
(GetCurrentThread), confirmed by the SDK import library. Trace length 46,733;
zero MEM_BAD_POINTER and zero video initialization errors under dummy video.
Original Approach SHA-256:
`142d797c3b8bc5de5f653bdfdf488bad7ccfee1ce51cd7100bf64a846592b06d`.
Artifacts: `build-approach-pointer-current/{result.json,stderr.log}`.

The corresponding Freelance probe executes real SetClassLong after the previous
heap/default-size fixes, then stops at missing PMWINX #457 (LoadAccelerators),
also confirmed by SDK import records. Trace length 22,392; zero MEM_BAD_POINTER
or dummy video errors. Artifacts: `build-freelance-pointer-current/`.
These are bounded startup gates, not full interactive application acceptance.
No user-ball files were modified and no proprietary guest bytes were patched.

Logs: `/tmp/open32-tls-tests.log`, `/tmp/open32-tls-build.log`,
`/tmp/approach-tls-original.log`, `/tmp/freelance-tls-original.log`.
