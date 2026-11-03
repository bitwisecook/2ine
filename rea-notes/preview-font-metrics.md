# Word Pro preview geometry: system dialog units versus Helv (2026-11-03)

The investigation began read-only; the verified coordinate fix and regression
results are recorded below. Video-agent original-caller evidence establishes
an unscaled188×243 preview in a247×204 client: source and destination sizes are
copied directly from the bitmap header. The original template control is165×126
dialog units. Current `WinMapDlgPoints` selects the window PS font and measures
the substituted8.Helv at6×13, producing247×204 through truncating division.

## Primary documentation

IBM *OS/2 Warp Version3 Presentation Manager Programming Guide, The Basics*,
G25H-7103-00, October1994, page15-6 defines dialog coordinates using the system
font: horizontal units divide average width by4, vertical units divide average
height by8. The guide's API section15-23 describes mapping relative to hwndDlg;
it does not state that a presentation-parameter font changes the coordinate
base. IBM *PM Programming Reference Volume1*, G25H-7190-00, page7-323 likewise
documents mapping and HWND validation. These older guides are corroboration;
the selected Warp4.52 original executable below establishes actual behavior.

Sources downloaded read-only from the Bitsavers mirror:

- https://ftpmirror.your.org/pub/misc/bitsavers/pdf/ibm/pc/os2/OS2_3.x/G25H-7103-00_OS2_WARP_V3_Presentation_Manager_Programming_Guide_The_Basics_Oct94.pdf
- https://ftpmirror.your.org/pub/misc/bitsavers/pdf/ibm/pc/os2/OS2_3.x/G25H-7190-00_OS2_WARP_V3_Presentation_Manager_Programming_Reference_Volume_1_Oct94.pdf

`pdftotext -layout` retained `/tmp/os2-pm-guide-basics.txt` (relevant text near
line21004) and `/tmp/os2-pm-reference-v1.txt` (near26613). Browser PDF fetches
failed; successful CLI reads supplied the inspected source text.

## Original executable evidence

Selected `analysis/compiler-provenance/pm-bundle/output.000.OS2_DLL_PMMERGE.DLL`
under the parent OS/2 workspace has SHA256
`f1f56353dde3c7bb4994ee0bfb903f9cb59cda1926993135f796c8cf433794c6`.
The original PMWIN forwarding module has SHA256
`7988bfdd400e33e3467e07ff965e4f22971f19bbee050f21c9d7cff6cf1f8035`.
Its public32-bit ordinal787 forwards to PMMERGE5231. The16-bit ordinal130
forwards to3165, whose thunk reaches the same implementation.

PMMERGE5231's LX entry is object4:0xCA784, preferred base0x1e830000, hence
preferredVA0x1e8fa784. This is an actual entry-table association, not a nearby
MAP guess. The function validates the HWND through a helper, then calls
0x1e8f8504. That helper maps coordinate arrays using only two globals,
0x13e85a8c(width) and0x13e85a90(height). It performs nearest rounding around
division by4/8 (including signed cases). There is no window PS, selected font,
or presentation-parameter query in the mapping path.

Initialization at0x1e8f48b5 supplies a228-byte output buffer based at
`ebp-0x10c`, a global HPS at0x13e85920, and copies buffer offsets0x64 and0x70
into those two globals. The SDK `h/os2def.h` FONTMETRICS layout identifies those
offsets as lAveCharWidth and lMaxBaselineExt. The query-shaped call has an
unresolved import fixup in the unrelocated disassembly; its public import name
was not assigned speculatively. The field mapping and cached global use are
directly visible independently of that name.

Object pages were expanded using the independently verified parent
`scripts/lx-resolve.py`; object4 is32-bit. `ndisasm -b32` decoded only bounded
entry/helper/initialization ranges. Retained bytes, disassembly and JSON live in
`build-lotus-visuals/preview-font-evidence/` (entry, mapping, cached-metrics and
evidence.json). Preferred addresses are not claims about a currently loaded
host process. No Ghidra pseudocode or guessed source label is required here.

## Original font metrics

FOCAMETRICS layout is from the actual DDK
`components-2004/combase/DDK/base/h/pmfont.h`. Font signatures and size fields
were validated and metrics read as the declared16-bit fields.

Original extracted HELV.FON has SHA256
`90bb13954bc73999fe1836eed35706c89b83c8aedf98b0814be5bde0912bcce0`.
Its RT_FONT resources were resolved through the original LX resource/object
tables, with EXEPACK page expansion, not by scanning compressed bytes.

| Source | Point size | DPI | Average width | Ascent+descent | Max baseline | External leading |
| --- | --- | --- | --- | --- | --- | --- |
| HELV.FON resource1008 |8 |96 |5 |11+2 |13 |0 |
| HELV.FON resource1009 |10 |96 |6 |13+3 |16 |0 |
| HELV.FON resource1030 |8 |120 |6 |13+3 |16 |0 |
| DDK sys10vgc.fnt, System Proportional |10 |96 |6 |12+4 |16 |0 |
| DDK sys10bgc.fnt, System Proportional |10 |120 |8 |15+5 |20 |0 |

DDK `video-1998-06/ddk/video/src/ps/isofonts/sys10vgc.fnt` SHA256 is
`aa12818941f209c0f6d4bd882609897b7e1e6b802057b60271b14c5796be71b3`.
The corresponding120DPI sys10bgc.fnt SHA256 is
`c4d2345b083345e464b0f5e7aef347211b24cb50182d3177d2bab17207605bfb`;
its8×20 metrics match rounding the96DPI system dimensions to120DPI.
The DDK Helv8 VGA sample independently has13baseline; changing Helv8 to16 at
96DPI would falsify both font sources. The sampled SYSFONT.DLL is a distinct
Mirrors font module and was not substituted as System Proportional evidence.

## Implication and remaining acceptance

Using the shipped96DPI system-font6×16 factors gives a248×252 pane with original
nearest rounding (247×252 with the current truncation), sufficient for188×243.
The current selected-Helv13baseline mapping explains the vertical crop. This is
a coordinate-base defect, not proof that FreeType's Helv baseline should grow,
nor a reason to scale the preview bitmap. Current substituted Helv average6
also differs from the original bitmap width5, but that alone does not cause the
vertical crop.

The fix preserves explicit presentation fonts while correcting the system
dialog-unit base and rounding. It affects all dialog template geometry, so
actual original Welcome/control containment and DPI96/120/144/192 need renewed
acceptance. No actual original-system execution was performed here.

## Production fix and regression gate

`pmwin/dialogs.c` caches the intrinsic System Proportional6×16 base scaled to the
validated current displayDPI. The cache is per host thread and refreshes when
that DPI changes; all windows share the same system dimensions regardless of
their selected presentation font. No PS acquisition occurs merely to map dialog
coordinates. The existing font-rendering path and its13-pixel Helv8 baseline
are unchanged. This does not add a configurable system-font family.

Both mapping directions use signed nearest rounding, with overflow preflight
before mutating any input point. The API keeps its32-bit POINTL range contract;
the original helper's16-bit input loads were observed but not introduced as a
new truncation behavior in the compatibility layer.

New `test_display_dpi.c` fixed tables cover96/120/144/192 dimensions, positive
and negative half/odd coordinates, inverse mapping, three different window
fonts, unchanged text metrics, overflow/no partial mutation, invalid handles,
empty/null arrays, and cache refresh after DPI changes. A real packed native
DLGTEMPLATE creates control25700 at165×126DLU, verifies its client dimensions
and188×243 bitmap containment. Before the production fix all four DPI tests
failed on the system-cell assertion. After fixing the test fixture's invalid
static-control style0 to text style1, all four pass (0.21s); this fixture repair
is separate from the meaningful pre-fix coordinate failure.

Logs: `/tmp/2ine-dialog-units-before-tests.log`,
`/tmp/2ine-dialog-units-focused-tests.log`,
`/tmp/2ine-dialog-units-final-build.log`. The complete coherent runtime suite
passed66/66 in29.90s (`/tmp/2ine-dialog-units-final-tests.log`), including retained
dialog pointer/paint/frame/menu, printer and Open32 tests. Source and build are
released for a fresh immutable original-Welcome visual acceptance; that visible
gate is not claimed by the native packed-template test.
