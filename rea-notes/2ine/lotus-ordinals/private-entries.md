# Private Open32 entries without packaged declarations

These observations supplement the ordinal census. They do not supply verified
C prototypes or authorize bridge registration. The original PMWINX SHA-256 is
`07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`.
The retained Ghidra record is
`ad985a9fc408fd5711f958f14f699b33664c183e3269639d0fc4bd876e11dd51`.
Its raw export, manifest and provider log are in
`/Users/jimd/.cache/os2-re-module/ghidra-jobs/analysis-9lwae4ej/`.

The SDK catalogue identifies these export names but contains no matching public
declarations. Targeted LLVM 21 disassembly of the hash-qualified cached function
chunks establishes the following observations. Addresses are preferred Program
addresses. The bytes are post-loader Program memory; unresolved import operands
and relocated data addresses are not original unrelocated values. Import targets
below were joined separately to exact manifest fixup positions.

| PMWINX ordinal | Entry | Observed interface and behavior |
| --- | --- | --- |
| 5000 `_CREATEBITMAPINDIRECT` | `0x80298` | Reads one stack pointer at `[EBP+8]`; reads fields at offsets 4, 8, 12, 16 and 18. Checks nonnegative dimensions, even stride and 16-bit fields no greater than 32. Uses PMMERGE 5424 `WinXXAllocMem`; multiple plain near returns. The complete input layout, object lifetime and fault contract require further qualification. |
| 5002 `_CREATEDIALOGINDIRECTPARAM` | `0x6b9dc` | Reads five stack slots through `[EBP+24]`. Normalizes first argument −1 to 0 and third argument 0 to 1; invokes internal helper `0x37128`; plain near return. Resource/template layout and callback contract remain unqualified. |
| 5004 `_DIALOGBOXINDIRECT` | `0x6b99c` | Forwards four stack arguments to entry 5005 with an additional zero argument; removes its own five outgoing arguments and returns with plain near RET. |
| 5005 `_DIALOGBOXINDIRECTPARAM` | `0x6d2d8` | Reads five stack slots. Processes the third argument using PMWIN 834 `WinQueryWindow` and 899 `WinWindowFromID`, normalizes first argument −1, then calls helper `0x373a4`. Plain near return; no public declaration established. |
| 6011 `WINXTRANSLATEMSG` | `0xa3678` | Reads two stack pointers. Queries PMMERGE 5447 `WinQueryDaxData` for the first input field; calls conversion helpers and writes seven consecutive DWORDs to the second pointer. Both decoded exits use plain near RET. Exact message structures and translation semantics remain unqualified. |

The plain RET observations distinguish the mechanism from a decoded `RET n`;
they alone do not establish parameter order, signedness, pointer ownership,
calling convention or safe implementation. Ghidra's automatically selected
Windows/default-stdcall model is not evidence for an OS/2 ABI.

DOSCALLS 547 `THK_UNITHUNK` is also named but lacks a packaged declaration. It
requires analysis of the original DOSCALLS provider and its importing call sites;
it must not receive an ordinary guessed API signature.
