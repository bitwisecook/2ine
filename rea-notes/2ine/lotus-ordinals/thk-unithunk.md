# DOSCALLS ordinal 547: universal thunk evidence

**Ordinal 547 is a register/descriptor-driven 32-to-16-bit dispatcher, not an
ordinary fixed-argument C function.** The legacy ordinal table calls it
THK_UNITHUNK. The hash-bound original SYM names its entry **THK32_UNITHUNK**.
No ordinary prototype or runtime implementation is proposed here.

## Selected original and authority

The reference provider is
`extracted/install-media/os2image/disk_2/DOSCALL1.DLL`, resident name DOSCALL1,
SHA256 `c1e39d8220b38de30f3f83ac33044e116cbe2ed384edf64160e8b1b8dd8cda16`.
The debug/UNI copy has identical bytes. Its ordinal-547 entry is **object 2,
offset 0x760c**, USE32, preferred address `0x1c02760c`. This entry has no encoded
export-name record; the name is qualified by the exact bound SYM, SHA256
`fb23a193f1b0b3e89999ac90a379ddc822a162cc62998a4db4acf89ac67b76d1`.
SMP is a different provider, SHA256
`ceb107acfc3de0d2c32feffb22a95d9cd63a797641d9d6e1c141fb49b7226129`,
whose same ordinal is object 2+0x7624. Its offsets must not be transplanted.

The exact-hash shared module-cache lookup returned no match. Rather than
select another build by name, this analysis reused the separately hash-bound
workspace product `analysis/decompiled/DOSCALL1/retail`: manifest input and SYM
hashes were rechecked against actual files, and the original entry table was
rescanned with the installed module skill. Raw pages were independently
expanded using `scripts/unpack-lx.py` and disassembled with ndisasm in each
object's actual 16/32-bit mode.

That older Ghidra decompile warns about bad instruction data and proposes
`__regparm3`. Neither that prototype nor its 492-byte, disjoint inferred body
is authority for the contract. The assembly addresses, original fixups and
cross-segment byte sequences below are the evidence.

## Actual importing stubs

The current Lotus ball's original **LIBUNI.DLL**
(`9a2fb16958c637eca1214415b3f0583df9e912fbbbe7d835acc19bd7577a21ec`)
and **UCONV.DLL**
(`e81c8bdfee0cf3fa0352d42094b34e1cf454fe631f0f895ef84406f35d1ace80`)
each contain two stubs. They are OS/2 support DLLs in the optional installed
closure, not directly established five-application startup calls.

| Importer | Stub preferred address | EAX target | EDX | ECX |
| --- | --- | --- | --- | --- |
| LIBUNI | 0x1caf8 | DOSCALLS#162 DosFSRamSemClear | 0x5d1 | 0 |
| LIBUNI | 0x1cb0c | DOSCALLS#161 DosFSRamSemRequest | 0x11d2 | 0 |
| UCONV | 0x14054 | DOSCALLS#162 DosFSRamSemClear | 0x5d1 | 0 |
| UCONV | 0x14068 | DOSCALLS#161 DosFSRamSemRequest | 0x11d2 | 0 |

Each unrelocated byte sequence is:

```asm
mov eax, 0             ; OFFSET16 fixup in low half, SELECTOR16 fixup in high half
mov edx, <descriptor>
sub ecx, ecx
jmp <DOSCALLS#547>      ; relative32 import fixup; preserves original caller stack
```

This is four actual source locations represented by two counted source-list
fixup records, not two runtime calls. Preferred addresses are file-analysis
coordinates, not a running guest's loaded addresses. The separate selector and
offset fixups prove EAX is a **16:16 target pointer**, not a normal scalar API
argument. EDX/ECX supply descriptor bits; arguments remain in 32-bit stack slots
following the original near return address.

## Observed register, stack and selector protocol

- Entry saves EBP, EBX, ESI and EDI. EAX is retained as target EDI. EDX and ECX
  are saved into descriptor state. No evidence supports treating these as three
  ordinary source-language arguments.
- For these two descriptors, the low nibble gives one or two argument slots.
  Low nibble 0xf has separate prefix/flag handling, so a complete grammar must
  account for that escape rather than assuming every nibble is a count.
- Argument slots are read from `[original EBP+8]`, advancing four bytes. A
  descriptor with low two bits 00 pushes a full 32-bit value; 10 pushes AX;
  odd forms consume additional pointer-description bits. The original includes
  null handling, string/size forms and 64-KiB-boundary copying/restoration paths.
- Pointer conversion calls the hash-bound `DOS32FLATTOSEL` helper (object
  2+0xc88). Returned pointer forms can use `DOS32SELTOFLAT` (2+0xd54).
  Temporary copy blocks and an exception-handler path are present. Their
  complete failure/reentrancy semantics are not reconstructed here.
- `UT_SwapStack` (2+0x724c) selects/maintains a thunk stack. The thunk forms an
  SS selector from the stack address high word, TI bit and existing privilege
  bits; it does not simply reuse a flat host stack pointer.
- At 2+0x774d it performs an operand-size-prefixed far transfer into USE16
  object 3, label `UT32_TARGET` at +0xc49c. The decisive original bytes there
  are `67 36 ff 5b 04`: **16-bit far CALL through SS:[EBX+4]** using a
  32-bit address override. That saved frame slot holds the EAX target.
- The following bytes `66 ea 64 77 02 1c 5b 00` transfer back to the flat
  32-bit return path at 2+0x7764 in this original mapping. Original selector
  constants/relocations are not portable 2ine host-selector assignments.
- Return code restores saved stack state with LSS, assembles the target's DX:AX
  result into EAX, and applies descriptor-controlled result conversion. It
  restores EBX/EDI/ESI/EBP and ends with **plain near RET (`c3`) at 2+0x77db**,
  not RET-immediate. The original caller's stack arguments therefore remain
  for caller cleanup. ECX/EDX/flags are not qualified as preserved.

The two observed descriptors decode consistently:

| Descriptor | Count | Parameter descriptors | Remaining result selector |
| --- | ---: | --- | ---: |
| 0x5d1 | 1 | pointer form 0x1d | 2 |
| 0x11d2 | 2 | pointer form 0x1d; full 32-bit value | 2 |

For remaining result selector 2, the original path masks EAX to 0xffff. The
actual 16-bit semaphore entries end with RETF 4 (#162, object 1+0x10f) and
RETF 8 (#161, object 1+0x5c), matching their constructed 4-/8-byte target
argument blocks. This supports the observed argument/result conversions;
it does not qualify every descriptor value.

Authentic `ddk/components-2004/combase/DDK/base/h/bsedos16.h:1273–1274`
declares the Request/Clear parameter lists; its macro/type environment is
conditional and is retained as declaration evidence, not an automatically
applied ABI. The shared catalogue currently has no declaration for #547.

## Consequences and remaining limits

A native replacement cannot safely be a success stub or ordinary `_System`
wrapper with guessed stack-byte count. It must preserve the incoming target,
descriptor registers, original stack/return relationship, 16:16 translation
and target invocation/result behavior. The original DOSCALL1 also contains
privileged/kernel-dependent code and cannot simply be loaded as an ordinary
user DLL to obtain this dispatcher.

This is static evidence only. No user application was launched and no original
file was modified. The full descriptor grammar, exception/stack-growth paths,
copy-back rules and every return policy still require bounded reconstruction
and generated ABI tests before implementation. [thk-unithunk-evidence.json](thk-unithunk-evidence.json)
retains source hashes, exact stub bytes/fixups and the qualified observations.

## Established descriptor grammar (6 November 2026)

Decoded from original instructions at object 2+0x7623..0x79f8 and the USE16
transfer at object 3+0xc49c, read from the hash-bound `02-CODE/listing.asm`.
This supersedes the "full descriptor grammar" limit above. The descriptor is one
64-bit little-endian bit stream, ECX:EDX, consumed from bit 0.

**Prefixes** (2+0x7623, repeated while the low nibble is 0xf):

| Bits | Width | Effect |
| --- | ---: | --- |
| low5 = 0x1f | 6 | Sets bit 1 of the per-thread word at `pTLMA0`, then re-tests. At 2+0x7880 the flag suppresses DOS32FLATTOSEL, so pointers reach the target flat. |
| low5 = 0x0f | 5 | Sets status bit 2 and, when ESP is inside the thread stack, registers the handler at 2+0x7dc9. On 0xC0000005 it unwinds and returns 0x57 to the 32-bit caller. Ends the prefix loop. |

The 0x1f flag is a contract between DOSCALL1 and kernel 16-bit entries. Eleven
other DOSCALL1 sites set the same bit before far-jumping into the kernel.

**Count:** the next 4 bits give the source slot count. Each slot is 4 bytes,
read in order from [EBP+8]. Each is pushed as it is read, so the first slot sits
at the highest 16-bit address (Pascal order).

**Per-slot element:**

| low2 | Width | Pushed |
| --- | ---: | --- |
| 00 | 2 | Full 32-bit slot |
| 10 | 2 | Low word |
| x1 | 5, or 9 if low5 = 0x1f | 16:16 pointer. Bit 1 is copy-back. |

A null pointer is pushed as 0 with no sizing.

**Pointer size code**, bits 2..4 of the 5-bit form (k):

| k | Size in bytes |
| ---: | --- |
| 0 | Input: strlen+1. Copy-back: writes 0 to pointee+2, then size 2 (USHORT result in ULONG storage). |
| 1..4 | 1<<k: 2, 4, 8, 16 |
| 5 | Value of the previous slot |
| 6 | Value of the next slot. If the next element's low bit is set, that slot is a pointer to the count, read through once. |
| 7 | No size and no tile check |

**Extended form**, low5 = 0x1f: 4 bits are consumed first, so copy-back is
original bit 5 and the code x is bits 6..8; 5 more bits are then consumed.

| x | Size in bytes |
| ---: | --- |
| 0 | previous slot * 4 |
| 1 | next slot * 4 |
| 2 | next slot * 8 |
| 3 | next slot * 16 |
| 4 | first USHORT of the pointee |
| 5 | 24 |
| 6 | 12 |
| 7 | No size and no tile check |

**Tile rule:** a sized pointer is copied when `ADD CX,[ptr low16]` carries, that
is `(uint16)size + (uint16)pointer > 0xffff`. An object ending exactly at a tile
boundary is therefore copied too. The temporary is converted in place of the
original. Copy-back runs in list order after the target returns and before result
conversion, masking the size to 17 bits. An allocation failure returns the
allocator's error straight to the caller.

**Tail:** while bit 2 is set, consume 3 bits. Code 0 pushes a zero word, code 1
a zero dword, and codes 2 and 3 push nothing.

**Result selector:** the remaining low two bits select the result. EAX is DX:AX
beforehand.

| Code | Result |
| ---: | --- |
| 0 | DX:AX unchanged |
| 1 | DOS32SELTOFLAT |
| 2 | Zero-extended AX |
| 3 | Sign-extended AX |

**Conversion helpers:** DOS32FLATTOSEL returns values below 0x10000 unchanged,
returns 0 for addresses at or above 0x20000000 (except 0xffffffff), and maps
0xffffffff to itself. DOS32SELTOFLAT returns values below 0x10000 unchanged.

**Stack:** UT_SwapStack stays on the caller's stack while SP > 0x1000 and moves
to a thunk stack otherwise. The target must remove its own arguments, because the
return path reloads SS:ESP from the slot directly above them. ES and DS keep their
flat values across the transfer.

### Validation against the original's own stubs

DOSCALL1 has 53 of its own `JMP THK32_UNITHUNK` stubs. A static scan with this
grammar parsed all 53 and left no unconsumed bits above the result selector in any
of them. The forms seen were:

| Form | Stubs or uses |
| --- | ---: |
| Result 2 | 51 |
| Result 0 | 2 |
| 0x0f prefix | 41 |
| 0x1f prefix | 15 |
| u16 slot | 73 |
| u32 slot | 17 |
| k=0 pointer, copy-back / input | 28 / 21 |
| k=2 pointer, copy-back | 7 |
| k=5 pointer, copy-back | 1 |
| k=6 pointer, copy-back / input | 16 / 6 |
| k=7 pointer | 1 |
| Tail code 0 / 1 | 2 / 8 |

No stub uses the extended pointer form, so that branch rests on the instructions
alone. Neither do any use result selector 1 or 3. This is a parse check of static
bytes; no descriptor was executed.

Separately identified: UT16_TARGET (2+0x7568) is the reverse 16-to-32 direction
and is not reached through ordinal 547. The entry at 2+0x7a44 is a hand-coded
variant whose symbol was not established. Neither is part of this grammar.

