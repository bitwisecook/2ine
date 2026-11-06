# Approach: original Open32 interlocked contracts

After Unicode2073 was implemented, the private Approach probe stopped at
PMWINX423. The installed os2-re-module catalogue and exact-hash permanent
analysis `ad985a9fc408fd5711f958f14f699b33664c183e3269639d0fc4bd876e11dd51`
were reused; original provider SHA is
`07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`.
No new Ghidra analysis or original execution was needed.

| Ordinal | Name | Preferred entry | Bytes | Arguments/cleanup |
|---|---|---|---:|---|
| 422 | InterlockedExchange | 0x10012 | 12 | pointer,value;8bytes/caller |
| 421 | InterlockedDecrement | 0x10000 | 18 | pointer;4bytes/caller |
| 423 | InterlockedIncrement | 0x1001e | 18 | pointer;4bytes/caller |

The SDK declarations at os2win.h6598–6602 are LONG WINAPI, PLONG operands.
Public wrappers load their arguments from ESP+4/8 and end in near RET without
an immediate. The original increment/decrement **return the sign of the new
value**, not necessarily the new value. Both use this exact sequence:

```
MOV EAX,[ESP+4]
LOCK INC DWORD PTR[EAX]       ; DEC for421
LAHF
SHR AH,6
MOVZX EAX,AH
DEC EAX
NEG EAX
RET
```

After LAHF, SF/ZF shifted right6 yields2/1/0 for negative/zero/positive,
then DEC+NEG yields−1/0/+1. Memory remains the complete wrapping32-bit value.
Exchange uses LOCK XCHG [EDX],EAX and returns the previous DWORD. No error
state mutation or pointer/alignment checks are present in these small bodies.
The x86 instructions accept unaligned operands but require a writable complete
span. A portable native implementation/guest bridge must not perform shadow
copy-in/copy-out RMW, which would lose atomicity. Cross-process mapped shared
memory and unaligned spans require explicit qualification. Existing external
batch_services_basic.c increment/decrement definitions return the full new
value and therefore do not match this original provider.

The parent owns public registration and removes the duplicate batch definitions
when integrating the verified adapter below.

## Implemented adapter and checks

`os2libs/pmwinx/atomic.c` maps its public PLONG back to the actual guest word
using `lx_execution_address(...,4,READ|WRITE,...)`, then invokes runtime atomic
operation2(add) or1(exchange). It never dereferences a typed unaligned LONG,
performs a host-side shadow RMW, or replaces guest backing. Unsigned addition
models32-bit wrap; sign conversion is explicit. Success preserves last-error;
an inaccessible/non-guest operand is rejected with87 before mutation. This
checked adapter behavior replaces an original hardware fault on bad pointers;
it is not a claimed original error return contract.

Unicorn serializes operations with its guest CPU mutex and uses byte-safe wire
loads plus CPU writes; Linuxi386 uses LOCK XADD/XCHG. The word must fit one
writable guest mapping; intra-mapping page crossings work. Separately mapped
adjacent pages are not jointly borrowed by the current runtime. Unicorn locks
are local to one execution instance, so this does not establish cross-process
atomicity for shared memory. Nativei386 LOCK instructions have stronger host
cross-process behavior, but that provider was source-reviewed rather than run
on this Darwin acceptance host.

The real-backend unit `tests/os2libs/test_open32_atomic.c` passed outside the
macOS sandbox in0.23s. For unaligned and cross-page operands, four host API
contenders each make2,000 updates while a guest x86 LOCK INC/DEC loop makes
100,000 updates: exact108,000 followed by0 in each placement. This checks
432,000 total real-backing updates across guest CPU and host calls. Additional
checks cover sign vs value, INT_MAX/MIN wrap, Exchange previous bits, error
preservation, read-only mappings, host-local/poison/stale pointers and boundary
spans. Earlier fixture-only HAL join success polarity was corrected before the
final passing run.

`tests/integration/open32_atomic.py` checks public LX ordinals421/422/423,
caller stack cleanup4/8/4, unaligned/cross-page backing, source guards and
poison/wrapping/read-only pointer failures. Structural EXE/data-page assertions
and Ruff/ty checks pass; parent owns its final integrated emulation gate.
