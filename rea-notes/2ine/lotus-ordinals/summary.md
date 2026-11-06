# Lotus missing ordinals: frozen source audit

The frozen audit finds **393 missing exports in selected 2ine providers**:
277 referenced by the five applications' static closures, 24 additional ones
from previously verified dynamic startup closures, and 92 only in other
installed Lotus modules. **All 393 ordinals resolve to catalogue names**;
377 have packaged SDK declaration candidates, and direct authentic SDK header
checks add ten PMWP declarations (seven data objects and three class factories).
That leaves six unresolved declaration contracts. There are 57 packaged manual
excerpt/reference matches and no flagged conflicting export names. This is an export
coverage inventory, not proof that every imported function executes or every
present export implements its full contract.

Every missing ordinal is enumerated in [missing-apis.csv](missing-apis.csv),
including names/aliases and their source hashes, all declaration and ABI
candidates, documentation references/coverage, importer paths and scope.
The CSV also includes **52 separately marked original-provider interfaces**
(51 SOM and HELPMGR#53 Win32QueryHelpInstance). Those are absent from the built
native tables but **are not missing selected-native exports**: this ball's
policy selects original OS/2 DLLs for them. The 445 rows contain no inferred
function prototype applications. LF line endings are intentional.

## Scope and reproducibility

Source HEAD at capture was `99f59582b7a83e9ea9792010f5654c4dad2a9c43`, with
subsequent uncommitted API implementations. The census used an explicit copied
source/generated-export snapshot; no captured source changed during that scan.
The exact hashes, 411 binary input identities, policy inputs, application
closures and artifact hashes are in [provenance.json](provenance.json).
HEAD alone does not identify the export snapshot. The current source/runtime
may already contain later changes.

The module skill and shared API/cache references were read. The actual
`scripts/os2-re-module api scan` inspected the five original application roots
without launching them. The same installed `os2api.load_catalogue/lookup` API
was used once per missing interface with a single catalogue load. Explicit
`api lookup PMWINX 262/636 --docs --json` controls and exact-hash module-cache
lookup were retained. The existing Freelance hash cache query returned no
match, which was preserved as a cache miss, not replaced by a name match.

`tools/lotus_imports.py` scanned the original ball read-only using the frozen
source snapshot and cross-checked its parser against `build/runtime/lx_dump`.
Scratch evidence is under `build/lotus/ordinal-audit/`: census JSON/CSV/Markdown,
compressed exact fixup references, full compressed catalogue evidence, skill
scan results, copied source and capture/resolve/finalize scripts. No guest
execution, build or central source edits were performed for this audit.

## Families

| Selected native provider | Missing exports | Five-app closure union | Optional only |
| --- | ---: | ---: | ---: |
| PMWINX | 234 | 214 | 20 |
| PMGPI | 57 | 35 | 22 |
| PMWIN | 34 | 26 | 8 |
| PMWP | 32 | 1 | 31 |
| PMDRAG | 15 | 15 | 0 |
| DOSCALLS | 10 | 6 | 4 |
| MMIO | 4 | 0 | 4 |
| PMSHAPI | 3 | 2 | 1 |
| PMCTLS | 2 | 2 | 0 |
| PMSPL | 1 | 0 | 1 |
| SO32DLL | 1 | 0 | 1 |

The larger groups include Open32 window/graphics/registry/DDE interfaces,
GPI path/area/metafile/palette operations, PM drag-and-drop and window services.
PMWP has many class-data/class-construction interfaces in optional DLLs; its
only five-app closure gap here is #286 WinOpenObject. A class-data-like name
alone does not establish the original storage layout.

| Application | Static closure binaries | Missing selected-native interfaces, including verified dynamic roots |
| --- | ---: | ---: |
| Approach | 31 | 208 |
| Organizer | 29 | 200 |
| Lotus 1-2-3 | 3 | 166 |
| Freelance | 13 | 127 |
| Word Pro | 6 | 9 |

Counts overlap across applications and must not be added. Verified dynamic
roots are MAIN123W.DLL, Organizer OR1C30EN.DLL, and Freelance FLWRSEN/FLWRS8EN;
their prior observation sources and hashes remain separate from static imports.
Across all applications there are 49 distinct static binaries and 9 additional
verified-dynamic-only binaries; 353 other installed-module records are optional.

Word Pro's nine gaps in this snapshot are DOSCALLS#237/#238
ResumeThread/SuspendThread; PMGPI#465/#466 QueryMetaFileBits/Length and #597
QueryPaletteInfo; PMWIN#705 CancelShutdown, #712 CpTranslateString, #774
IsWindowShowing; and PMWINX#6011 WinXTranslateMsg. This list is static coverage,
not a startup execution trace.

## Original PMWINX code evidence

The parent obtained a full hash-bound static Ghidra snapshot for original
PMWINX.DLL SHA256
`07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`.
The permanent module-cache analysis is
`ad985a9fc408fd5711f958f14f699b33664c183e3269639d0fc4bd876e11dd51`;
the durable raw job is
`~/.cache/os2-re-module/ghidra-jobs/analysis-9lwae4ej/analysis.json`.
Its source hash/coverage are in provenance. It reports 1,580 functions, 795
encoded exports, 779 bound export addresses and no reported truncation.

All 234 missing PMWINX ordinals match encoded exports. Of those, 231 match an
exact recovered function entry; 223 have decoded near RET observations and
8 do not. The other three are **encoded forwarders**, not missing code:

| PMWINX ordinal | Name | Encoded target |
| ---: | --- | --- |
| 551 | RegQueryInfoKey | REGISTRY#26 |
| 552 | RegQueryValue | REGISTRY#30 |
| 554 | RegSetValue | REGISTRY#42 |

These have no PMWINX bound code address or containing function. The CSV retains
forwarding targets, rather than assigning an invented local implementation.

The eight exact-entry cases with no decoded return are #102 DdeAccessData,
#103 DdeAddData, #108 DdeCreateDataHandle, #115 DdeGetData, #201 FatalAppExit,
#431 IsBadHugeReadPtr, #432 IsBadHugeWritePtr, #508 OutputDebugString. They have
recovered bodies at their exact export addresses; absent RET observation does
not establish a missing body, a noreturn contract or a calling convention.

The exporter selected `x86:LE:32:default`, compiler model `windows`, whose
default prototype is `__stdcall`. **That default is not the authentic OS/2
ABI.** The matching SDK WINAPI declarations are conditional `_System`
candidates. Decode contexts and plain RET instructions are recorded separately;
none of these facts alone proves parameter order, storage, pointer layout,
cleanup or semantics. No cross-ordinal prototype application is authorized.

## Remaining evidence limits

Sixteen selected-native gaps initially had no packaged declaration. Direct
read-only SDK checks corroborated ten PMWP rows: seven exported class-data
objects (#11/#14/#23/#35/#47/#311/#323) and three NewClass factory functions
(#18/#315/#318). Both C `.h` and C++ `.xh` declarations, raw source hashes,
exact lines and snippets are included in CSV/provenance. Class-data objects
have **no function calling convention** and no inferred field offsets/size.
Factories retain SOMLINK tokens; the C header's explicit system-linkage pragma
is recorded separately. The C/C++ return type spellings remain variants.
No original DLL layout or compiler application profile has been verified.

The six remaining unestablished declaration contracts are DOSCALLS#547
THK_UNITHUNK, PMWINX#5000/#5002/#5004/#5005 internal bitmap/dialog adapters,
and PMWINX#6011 WinXTranslateMsg. Further targeted disassembly observations for
the five PMWINX entries are recorded in [private-entries.md](private-entries.md),
including observed argument slots and separately resolved imported helpers.
Complete prototypes/storage remain unqualified. Header-derived
candidates elsewhere retain conditions and source hashes; mixed 16/32-bit
type variants are not compiled layouts.

The targeted [DOSCALLS 547 analysis](thk-unithunk.md) establishes a universal
32-to-16-bit thunk with a 16:16 target in EAX and a descriptor in EDX/ECX.
Actual LIBUNI/UCONV stubs, original far-call/return paths and descriptor-specific
argument conversions are recorded. It requires a dedicated dispatcher contract;
an ordinary C prototype would misrepresent it. Full descriptor grammar and
failure/copy-back behavior remain unqualified.

The 57 documentation matches are packaged manual excerpts or lexical
references with source/page/section hashes. Some occur in a related API's
chapter; they are not silently promoted to complete API contracts. The other
336 selected-native gaps have no packaged manual evidence in this catalogue.
Full stored source pages can be queried separately through the DLL document
cache where present.

There are zero import parse errors and zero unresolved modules in the five-app
static closures. Existing INIEDIT.EXE has a malformed nonresident export-name
table; its imports decode completely, exports do not. LTFLEG10.DLL is a
non-image/unsupported record and was not treated as an empty decoded DLL.
The optional corpus has 19 unresolved module references, listed in provenance;
these are separate from the ordinal count. The independent lx_dump check
matched 405 binaries; five forwarded-export cases were intentionally not run
because that older dump decoder does not advance its forwarder cursor.

PMWINX#262 GetCursorPos and #337/#636 priority APIs were already present in
the frozen generated tables and are not counted missing. The parent reports
#606 SetCursorPos implemented/integrated **after this snapshot**, and Unicode
#2073 work is forthcoming; these later states do not silently reduce the
393 count. A fresh census after integration is required for a new total.

The original user ball was read only. Full proof data and the exact copied
source snapshot are archived, compressed, at
`/Users/jimd/src/os2/.cache/os2-reports/2026-11-05/lotus-ordinal-audit/`.
Its `archive-receipt.json` retains original and archive hashes; provenance links
that receipt. The completed `build/lotus/ordinal-audit` subtree was removed at
handoff after the Unicode analysis and all other consumers released it. Use
the permanent archive and module cache for further lookup.
