# Original Word Pro SmartMaster preview

The qualified coherent Word Pro trace reaches actual preview painting, not only
file selection. `coherent-tabs-wordpro.stderr` creates static control 25700 at
x244/y39, size247×204, then subclasses it with the application's procedure. Its
WM_PAINT at line 5961 configures GPI attributes and requests PMGPI ordinal 603;
`unavailable export PMGPI#603` immediately follows. The nearby tab drawing requests
missing ordinal 599. The local OS/2 Toolkit 4.5 headers and the bundled ordinal
catalogue identify these as GpiDrawBits and GpiQueryBitmapBits.

The selected original `LOTUSW4/SMASTERS/WORDPRO/letter1.mwp` is 24300 bytes,
SHA-256 4d182778d63fd557f7a3e8af2829c3762c43eec19b29eb7b35c9681c72f2d081.
The trace opens and reads its actual WordPro/Bento data before WM_PAINT.
No synthetic document or replacement preview was supplied.

## Implementation and contracts

PMGPI now exports 599/603 with 20/28-byte caller-cleaned LX bindings matching
`os2tk45-master/h/pmgpi.h`. Guest bridges validate the complete point array,
header/RGB table, DWORD-aligned rows and selected-bitmap output extents before
calling native code. A query's dimensions are outputs; only format/plane fields
are required as inputs. The SDK signature and the original Microsoft OS/2 API
reference establish that contract ([Programmer's Reference volume 4](https://www.bitsavers.org/pdf/ibm/pc/os2/OS2_1.x/Microsoft_-_OS2_Programmers_Reference_Volume_4_Version_1.2_1989.pdf)).

GpiDrawBits decodes the raw bitmap through the existing checked bitmap reader,
maps destination coordinates, samples the specified source rectangle, and uses
the shared ROP/pattern/clip writer. BBO_OR and BBO_AND compression combine the
original indexed values before palette lookup, preserving inverted/arbitrary
palettes. BBO_IGNORE drops samples without combining them. Source handles and
scratch pixels are released; the destination's selected bitmap/attributes stay
owned by its presentation space.

GpiQueryBitmapBits returns bounded scan rows in requested 1/4/8/24/32-bit packed
format with DWORD padding, bottom-up ordering, dimensions and RGB/RGB2 palette.
It preserves available original indexed colors and quantizes when conversion is
necessary. Unsupported plane/depth/header combinations fail. Missing/invalid
selected bitmaps or output buffers return -1.

The original tab mask's valid destination starts at y=-2. The existing generic
bitmap blitter tried to read the entire out-of-bounds destination for AND/XOR
ROPs and failed. It now reads only the target intersection into its bounded
scratch buffer; the existing write clip suppresses pixels outside the target.
This also applies to other callers of the same bitmap blitter.

Covered options are BBO_OR/AND/IGNORE, 3/4-point rectangles, uncompressed
1/4/8/24/32-bit source data and 12/64-byte OS/2 info headers. Palette-index and
no-color-info flags currently fail explicitly with FUNCTION_NOT_SUPPORTED;
compressed images, mirroring/rotation and multi-plane formats remain outside
this implementation. Original Word Pro uses the covered BBO_IGNORE path.

## Verification

`bitmap_bits` tests actual pixel values, indexed compression with arbitrary
colors, upscaling, XOR inversion, clipping, negative destination ROPs, palette
and scan-line readback, row clamping, padding and canaries. `bitmap_bits_guest`
executes generated LX calls to the real599/603 exports, verifies BGR output
bytes and unchanged guard words, and rejects unmapped bits/info/point pointers.
Existing `presentation_manager` also passes. Combined: 3/3, 0.89 seconds,
`/tmp/2ine-preview-bitmap-final-tests.log`. Ruff, ty, generated bindings,
C formatting and source-boundary checks pass.

The original application was run for ten seconds against dummy SDL video/audio
using the separate private spelling-probe ball. No desktop input, visible UI,
active acceptance-ball write or Documents-ball write occurred. The run's actual
native-load inventory has PMWIN/PMGPI/PMWINX/VIOCALLS/lib2ine each exactly once
from one fresh runtime cache.

Before the clipping correction, the original 188×243 8-bit preview already drew
successfully; its tab masks revealed the separate negative-destination defect.
After correction, all seven observed original DrawBits calls succeed. The real
SmartMaster upload is:

- source 188×243, 8bpp;
- destination points (1,1),(189,244), source points (0,0),(188,243);
- ROP 204/SRCCOPY, BBO_IGNORE 2;
- GpiDrawBits result 1.

Original tab masks are 10×19, 1bpp, AND ROP 136, BBO_IGNORE; their y=-2 destinations
now also return 1. There are no missing 599/603 export messages.

Evidence: `build-lotus-visuals/preview-headless-x5cnyyfw/{stderr,result.json,bitmap-observations.json}`.
Trace is 4794048 bytes; stop after 10.83 seconds was deliberate, process status 0.
Reproducer: `build-lotus-visuals/probe-original-preview-headless.py`.
Earlier comparison: `preview-headless-6r3yupzs`. All runtime evidence is from
original Word Pro and the installed original SmartMaster, not only generated tests.

Integrated CUA acceptance now shows the original thumbnail, but its top is
clipped. A successful real upload does not establish full-pane fit or that every
SmartMaster image/unsupported bitmap format renders.

## Crop investigation

`preview-geometry-jnh31sv7` is a separate headless diagnostic. Its Runtime is a
copy of the immutable Modal Acceptance closure; only scratch PMGPI objects add
bounded observation. The live app, its runtime and its ball were not modified.
`geometry-evidence.json` records the original EXE hash, the loaded instruction
range hash and the actual DYLD load inventory. PMWIN/PMGPI/PMWINX/VIOCALLS/lib2ine
each load once from the same fresh private runtime cache.

The preview PS has page units 0, no explicit viewport mapping, and a 247×204 client.
The original four horizontal/vertical font-resolution DevQueryCaps calls return
96. DrawBits remains 188×243 at (1,1), so normal clipping removes the top 40 rows.
Neither a missing transform nor a one-pel endpoint convention explains that
height difference.

The captured original caller is 0x21fe4, in the function beginning 0x21d80. Loaded
instructions 0x21ea1–0x21ef3 construct destination (1,1) to (1+header.cx,1+header.cy)
and source (0,0) to (header.cx,header.cy). Thus the original intentionally uploads
this bitmap without fitting it to the current control. The dump is a read-only
snapshot of loaded/relocated original instructions, not a reconstructed source
file. `preview-code-disassembly.txt` was produced with ndisasm 32-bit base 0x20000.

REA's original WordPro import exposes `.image` and reports no analyzed procedure
at this caller; those failed lookups are retained as diagnostic limitations,
not interpreted as decompilation evidence. The bounded loaded-code observation
establishes the arithmetic independently. The prerequisite is dialog-unit
geometry: the original resource's 126-DLU pane became 204 pels with the
window-selected Helv metrics. The printing/font investigation subsequently
verified the original WinMapDlgPoints helper uses cached system-font dimensions,
not the window's selected font. Original System Proportional at 96 DPI has a
16-pel baseline, while original Helv 8 has a 13-pel baseline. The corresponding
WinMapDlgPoints correction produces a 248×252 pane in focused resource tests,
without changing Helv's text metrics. Integrated acceptance remains with the
root agent after the coherent runtime rebuild. No bitmap fit override was added.
