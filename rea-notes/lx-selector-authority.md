# Original LX selector-fixup authority (2026-11-03)

This bounded read-only investigation checks the original kernel's selector
resolution path. No loader source was edited here. It does **not** establish
that NRALIAS plus a missing OBJALIAS16 bit always permits a zero-based flat
selector, nor that the fixup should always allocate a new 16-bit alias.

## Exact artifacts and decoding authority

- Checked `extracted/install-media/os2image/debug/UNI/OS2KRNLB` under the parent
  workspace: SHA256
  `9d0d7c5c5d7c9f772a68f8b81b1b6e282e01dfdf650da5060039e8a4f2505282`.
  This is the exact target registered in `config/os2-symbols.json` with the
  checked MAP/SYM, not the approximately located retail/full-debug images.
- MAP SHA256 `47d7c3e0a816db20934c95e216fcaf4b1a88fb1f64b206660db49ac12de96fd0`;
  SYM SHA256 `dea32ab5cc62a3e279c330fb450eae72a68959d7d1c772e13ef48f5cafab120c`.
- Original private-fixture WORDPRO.EXE SHA256
  `bf122341b274d7bee0909b66ebd6f5bc884d407828105e532761eb405e0e4387`,
  path `build-lotus-visuals/WordPro.2ineball/drive_c/LOTUSW4/WORDPRO/WORDPRO.EXE`.

Kernel MAP object 0010 is actual LX object 16, preferred base 0x120000,
flags 0x2015(USE32). The parent independently verified page resolver
`scripts/lx-resolve.py` expands the original EXEPACK pages. Bounded
`ndisasm -b32` ranges are retained in
`build-lotus-visuals/selector-authority/` along with exact bytes.

REA/Ghidra 12.2 was invoked on the exact kernel and verified its symbol binding.
Its `_ldrGetSelector` pseudocode nevertheless decoded the function as 16-bit
and produced invalid mixed-mode expressions. That pseudocode is retained as
`rea-ldrGetSelector.json` for provenance but is **not semantic authority**.
The header-declared 32-bit bytes are used below. `rea-ldrExpandRecord.json`
returned plausible 32-bit parsing, independently cross-checked against bytes.
Neither pseudocode output is original source or a live execution observation.

## Verified original path

| Original symbol | Checked object 16 offset | Preferred address |
| --- | --- | --- |
| _ldrGetSelector |0x63330 |0x183330 |
| _ldrEnum32bitRelRecs |0x5e030 |0x17e030 |
| _ldrGet32TgtAddr |0x6d607 |0x18d607 |
| _ldrExpandRecord |0x6d8aa |0x18d8aa |
| _ldrApply32Fixups |0x6d050 |0x18d050 |
| _SELLaToSel |0x11e10 |0x131e10 |
| _ldrSetVMflags |0x5f1f0 |0x17f1f0 |
| selLDTall |0x5d284 |0x17d284 |
| _SELInitLDT |0x6c560 |0x18c560 |
| _selInitLDTSub |0x6c692 |0x18c692 |
| _PGSetTypeLDT |0x6c500 |0x18c500 |
| _pgwSetTypeLDT |0x6c2e0 |0x18c2e0 |

`_ldrGetSelector` returns shared ring selectors through a switch table. For
the ordinary class input with executable object flags 0x2005, it selects the
checked `_R3CS` global at 0xc54de (stored selector 0x5b). That fact alone does not
decide the NRALIAS path, which calls a different helper.

`_ldrEnum32bitRelRecs` calls ExpandRecord, Get32TgtAddr, then Apply32Fixups.
There is no alias-bit clearing between those calls. ExpandRecord retains the
raw source flags in expanded-record byte 12; selector-only source type 2 has no
target-offset payload, so its initialized offset remains zero.

For an internal reference, Get32TgtAddr obtains the OTE and computes the target
linear address from target offset plus OTE base. At0x18d673 it tests NRALIAS.
For the normal application case, the alias-marked path calls `_SELLaToSel` at
0x18d68e. It adjusts privilege/type bits in the returned selector, stores that
selector and masks the address to 16 bits. The non-alias path at 0x18d6d8 instead
calls `_ldrGetSelector`. These are distinct paths in the actual bytes.

`_SELLaToSel` performs an existing process-LDT lookup. It computes a candidate
from the linear address, checks process range fields and may inspect descriptor
bases. It does not allocate an alias. Apply32Fixups handles source type 2 by
copying the resolved selector from the result record before writing the source
locations; it does not change the descriptor's base/default mode.

For linear address 0x10000 the candidate calculation produces0x0c. If that
candidate is selected by the process-LDT checks, the caller's normal code/RPL
adjustment yields0x0f. This is conditional: no actual original process LDT or
range fields were observed in this investigation.

`_ldrSetVMflags` uses the checked `_objflags` table at 0xcaf3c. The table maps
OBJALIAS16 (0x1000) to VM flag 0x400000. Thus object-time alias provisioning is
separate from the NRALIAS fixup's existing-selector lookup. The full VM/LDT
object allocation path was not recovered to completion.

The checked `selLDTall` worker constructs descriptor base fields from its
explicit base argument (`[ebp+24]`), rather than forcing base zero. It clears
the D/B bit while constructing tiled entries and conditionally sets descriptor
byte 6 bit 0x40 on the final entry from the requested attributes. This verifies
that nonzero descriptor bases and 32-bit defaults exist in the construction
machinery; it does not identify WORDPRO's actual resulting descriptor.

SELInitLDT calls selInitLDTSub, which allocates0x10000bytes of LDT storage,
registers the PGSetTypeLDT lazy-page machinery and stores its address in the
process structure at+0x710. PGSetTypeLDT registers pgwSetTypeLDT as its callback.
The inspected initialization/page callback ranges do not directly install a
special flat code descriptor in the first two LDT entries. A scan for canonical
initialized flat ring3 descriptor byte patterns found none in the checked
kernel's initialized object bytes; dynamic construction is still possible.
This is not proof that entry1 has a particular base or D-bit.

## WORDPRO facts and unresolved choice

Object 1 has size0x57ade0, preferred base 0x10000 and flags 0x2005 (USE32,
executable/readable, no OBJALIAS16). Object 2 has 24 bytes and flags 0x1004
(USE16 executable with alias requirement). Its entire raw code is:

`36ff1f 66eaabc256000f00 9a55000f00 66ea42c356000f00`

Its entire original page fixup data is:

`32 00 03 01 0e00 0900 1600`

This is an alias-marked source-list selector fixup to object 1 at all three
selector locations. There is no 32-bit offset relocation on the far-jump
immediates. Module flags 0x80312 include SDK E32NOINTFIX(0x10); IBM's documented
fixup ordering permits selector/pointer fixups to remain when ordinary internal
offset fixups have been discarded and objects must use their preferred bases.
This supports interpreting linked32-bit code addresses as preferred linear
addresses, but still does not directly identify the selected LDT descriptor.
A zero-based CS would interpret0x56c2ab as object offset0x55c2ab
at the preferred object base; an object-based CS would interpret it as offset
0x56c2ab. Both offsets are within the object. Neither candidate's raw starting
bytes provide an independently established function boundary, and choosing a
candidate because it resembles a prologue would not be valid evidence.

The required target D-bit and base must therefore come from descriptor
construction or a live original-system observation, not from the mere fact
that the immediate is larger than 64 KiB. A 32-bit object selector with a nonzero
object base remains a live possibility. The inspected original code refutes
an unconditional "allocate a new 16-bit alias on NRALIAS" reconstruction, but
does not prove a zero-based flat fallback. Root's provisional fix and generated
transition tests are separate compatibility evidence, not proof of the missing
historical descriptor step.

## Primary format reference

IBM's [LX specification](https://komh.github.io/os2books/os2tk45/lxref.htm)
defines the object alias requirement and default-size bits separately. NRALIAS
is valid for source types 2, 3, 6; source type 2 has no target-offset field. The
specification does not explicitly supply the missing runtime-descriptor rule
for this object/selector-only combination. Local SDK `h/exe386.h` independently
confirms the flag values. Pointer 16:16/16:32 policy should not be generalized
from the selector-only case without additional evidence.

## Bounded compatibility correction

The final correction is narrower than either general descriptor policy discussed
above. WORDPRO's module flags are `0x80312`, including SDK `E32NOINTFIX` (`0x10`).
Its ordinary non-resource executable objects are mapped at their preferred
addresses. Internal offset fixups have been discarded; their linked linear
offsets must remain usable. For an alias-marked **selector-only** fixup targeting
a USE32 object without OBJALIAS16 in such an executable, 2ine now supplies its
zero-based flat code/data selector. It does not allocate a USE16 descriptor or
replace the adjacent prelinked offset. DLLs, relocated resource objects and
16:16/16:32 pointer-alias fixups are excluded from this compatibility rule.

The former code printed a warning and wrote zero to all three WORDPRO selector
locations. Unsupported alias cases now identify the module, source object/page,
source type and target object/offset, then stop rather than silently writing zero.
Invalid internal object indexes are rejected before accessing the object table.

Generated integration regressions check both flat code/data selectors and an
executed USE32 → USE16 → USE32 transition with a preferred linear return offset,
plus invalid zero/out-of-range target indexes. The original private WordPro load
also no longer emits the alias warning. This is compatibility and format evidence;
it does not establish execution of WORDPRO's particular 24-byte thunk or recover
the historical LDT entry construction. Original-system thunk reachability and
descriptor observation remain unverified. The broader arbitrary-base descriptor
prototype and its tests were saved as an ignored follow-up patch, not shipped.

Successful startup also does not independently prove this24-byte thunk executes.
A loader can resolve a selector for an unused thunk without observing any later
execution fault. A generated transition test establishes the compatibility
implementation's chosen behavior, not that original thunk's reachability.
