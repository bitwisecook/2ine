# Menu check metric

Freelance progressed through PMWINX.262/606 and reached the missing ordinal306.
The installed module skill/catalogue resolves306 to GetMenuCheckMarkDimensions,
with SDK declaration `LONG WINAPI GetMenuCheckMarkDimensions(void)`.

The original PMWINX provider is SHA-256
`07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`.
Its existing permanent full Ghidra analysis
`ad985a9fc408fd5711f958f14f699b33664c183e3269639d0fc4bd876e11dd51`
contains the complete body at preferred VA0xa18ec–0xa193c. No fresh target
analysis was needed. Cached Program bytes were independently decoded with
ndisasm in32-bit mode; this is an ordinary local decoder result, not new REA
Evidence or an authentic source declaration.

The encoded export and original fixups establish two calls:
PMWIN.760 WinGetSysBitmap(HWND_DESKTOP,SBMP_MENUCHECK6), then
PMGPI.573 GpiQueryBitmapParameters into a12-byte old BITMAPINFOHEADER. Width
is loaded from offset4, height from offset6; the result places unsigned16 width
in the low word and unsigned16 height in the high word. Either failed query
leaves the returned LONG zero. The function has no public arguments, restores
its16-byte local allocation, and returns with plain RET. Ghidra's global Windows
compiler-model choice does not authorize a calling-convention inference.

The implementation obtains those actual bitmap dimensions through the same PM
and GPI services, avoiding guessed pixels. The current native WinGetSysBitmap
returns a fresh owned snapshot; the transient handle is deleted after either a
successful or failed parameter query. This is the current backend ownership
contract, not an inferred claim about original OS/2 system-bitmap ownership.
No PM bitmap implementation or global ownership policy was changed.

The injected native test varies non-square and full16-bit dimensions, forces
initialization/load/query failures, and checks that every allocated transient
handle is released. It uses no SDL/runtime or actual GUI. The generated LX
fixture compares the packed result against a separately queried native system
bitmap, checks that native bitmap deletion is supported, repeats the query and
verifies ESP for argument_bytes0/callee_pop0. Both native and LX CTest cases passed after the coordinated rebuild (see
`build/lotus/unicode-metrics-ctest.log`). Freelance then executed306 successfully
and advanced to missing PMWINX.462 LOADMENU after bitmap/text setup. The private
run ended with main exit1 after3.27s; its private SOMDD service was stopped.
This does not verify a working Freelance UI.

`menu-metric-evidence.json` retains the exact provider/export/body/context/import
facts and implementation/test hashes. Broader metric indexes and guessed
Windows checkmark sizes were not introduced.
