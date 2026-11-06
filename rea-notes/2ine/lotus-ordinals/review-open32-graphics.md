# Open32 graphics review (94 PMWINX rows)

Unbuilt and unrun. This is a static review of bodies, bridges, declarations and policies against
original PMWINX SHA-256 `07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`
(`extracted/install-media/os2image/FI/DAX/DLL/PMWINX.DLL`). Nothing here is runtime evidence.

R = REAL, P = PARTIAL (stated limit), F = FAKE.
Before: R 50, P 43, F 1. After: R 67, P 27, F 0.
These are review judgements by reading, not test results.

## Evidence

Retained listings: `.cache/os2-re/lotus-export-batch-2026-11-06/evidence/open32_graphics/output/`.
Extra helpers were decompiled with patched Ghidra 12.2 (`-cspec gcc`, provisional model):
0x943f8 Escape dispatcher, 0x93930 result mapper, 0x8c25c extent setter, 0x799fc enhanced play,
0x31234 enhanced enumeration, 0x75f2c DeleteHDCObjects, 0x75fd0 HPSToHDC without driver, and the
EMF dispatch table at 0xe1730. That scratch was deleted.

- All 94 originals end in plain `RET`, so callee_pop 0 holds. Stack use matches the declarations,
  except that Escape, SetWinMetaFileBits and WinTranslateDeviceRects do not read their last word
  in the retained entry listing.
- EMF table: entries 1, 23, 28, 69, 79 and 96-101 share a low half of 0x0938. They are read as
  no-op handlers. This is an inference from the table values, not a decoded handler.
- SetAbortProc issues DevEscape 0x1f41 with {hdc, proc} and returns 1.
- Escape cases below 0x26 sit behind an unresolved jump table, so per-case output sizes are not
  recovered. Codes 12-14 use the Win32 POINT layout.
- Enhanced enumeration stops when a callback returns 0, so playback stops at the first failed
  record.

## Integration checks

Policies, declarations and bridges agree for all 94 rows, and api32.json matches the policy
fragment. No row has two definitions. GetUpdateRgn has one body (regions.c) and one bridge
(pmwinx_batch_services.h). Handle enum order is unchanged.

## Link defect

GetUpdateRgn used LX_PRIVATE `getAnchorBlockNoHAB` and `getWindowFromHWND`. It now calls
`lx_pm_window_damage`, declared OS2EXPORT without OS2APIINFO in `os2libs/pmwin.h` and implemented
in `os2libs/pmwin/invalidation.c`. The caller frees the copy; the flip stays in regions.c and the
fallback to the invalid rectangle moved into the helper. A scan of every LX_PRIVATE name in
os2libs headers against os2libs/pmwinx and guest/bindings/pmwinx* found no other use: the
`makeUnixPath` hits are the GLoaderState member. Remaining `Window`, `AnchorBlock` and
`PresentationSpace` access is layout only, reached through exported functions or thread state.

## Print abort hook

`print_boundary` in printing.c calls open32AbortCheck before device output in StartDoc, StartPage,
EndPage and EndDoc, and looks the DC up again because the guest procedure can end the job or
delete the DC. A FALSE return aborts the job and yields SP_APPABORT (-2). RestoreDC keeps the
current abort procedure.

## Changes beyond the 94 rows

SetMapMode now supports modes 2-7, with isotropic adjustment from the original 0x8c25c logic.
ExtTextOut works under mapping modes, with unscaled glyphs. Rectangle fills hatch and pattern
brushes. Intersect/ExcludeClipRect map through the DC. DIB_PAL_COLORS is accepted by the DIB
helpers. GetCharWidth is unchanged.

## Open question

`bitmap->pixels` row 0 is the bottom row, while CreateBitmap, GetBitmapBits and SetBitmapBits use
row 0 for the first DDB row, which Win32 defines as the top. `tests/integration/open32_patterns.py`
pins the current order, so it was not changed. The original helpers at 0x78eb8 and 0x78ac8 were
not read. Settle this against the original before treating rows 229, 591 and 5000 as complete.

## Classification

| Ord | Export | Before | After | Finding / remaining limit |
|---|---|---|---|---|
| 8 | AnimatePalette | P | P | count 0 and NULL checks now follow the original. Limit: true-color display, so already drawn pixels do not change. |
| 10 | Arc | R | R | No defect found. |
| 17 | BeginPath | R | R | No defect found. |
| 45 | CloseMetaFile | R | R | No defect found. |
| 54 | CopyMetaFile | R | R | No defect found. |
| 59 | CreateBrushIndirect | P | P | BS_DIBPATTERN and BS_DIBPATTERNPT added. Limit: BS_INDEXED (4) still refused. |
| 66 | CreateDIBitmap | P | P | NULL bits now drops CBM_INIT; unknown option bits accepted; DIB_PAL_COLORS added. Limit: no RLE or 16 bpp; info size must equal header size. |
| 70 | CreateEllipticRgn | R | R | No defect found. |
| 71 | CreateEllipticRgnIndirect | R | R | No defect found. |
| 77 | CreateHatchBrush | P | R | Rectangle ignored hatch and pattern brushes; it now fills through open32BrushPixel. |
| 79 | CreateIcon | P | P | NULL XOR bits now gives a monochrome AND-mask icon. Limit: one plane, depths 1/4/8/24/32. |
| 83 | CreateMetaFile | P | P | SetTextAlign and Intersect/ExcludeClipRect now recorded. Limit: DrawText, ExtTextOut opaque/clip/dx, clip regions, palettes and pattern brushes are not faithfully recorded; coordinates are signed 16-bit. |
| 89 | CreatePolyPolygonRgn | R | R | No defect found. |
| 90 | CreatePolygonRgn | P | R | count < 2 now fails with 87 as in the original. |
| 95 | CreateRoundRectRgn | R | R | No defect found. |
| 136 | DeleteEnhMetaFile | R | R | No defect found. |
| 159 | DrawFocusRect | R | R | No defect found. |
| 160 | DrawIcon | P | R | Now maps the logical origin to device coordinates. |
| 164 | Ellipse | R | R | No defect found. |
| 174 | EndPath | R | R | No defect found. |
| 178 | EnumEnhMetaFile | P | R | dc without bounds fails 87, degenerate bounds return 0, table objects are deleted after enumeration. Record coverage limit is that of 515. |
| 180 | EnumFonts | P | P | No match returns 1 as in the original; non-TrueType type is 2. Limit: at most 12 fonts per query. |
| 188 | EqualRgn | R | R | No defect found. |
| 189 | Escape | P | P | SETABORTPROC now takes the procedure from the input pointer in the bridge. Limit: only the handled escapes work. The 8-byte POINT output for codes 12-14 follows Win32; the original jump-table cases were not recovered. |
| 196 | ExtCreatePen | P | P | Now follows the original: PS_USERSTYLE fails 50, cosmetic pens must be width 1 solid, geometric brush style <= 8. Custom dash code removed. Limit: round caps and joins only; width <= 512; BS_INDEXED brushes fail. |
| 199 | ExtSelectClipRgn | R | R | No defect found. |
| 208 | FillRgn | R | R | No defect found. |
| 221 | FrameRgn | P | R | Width and height use absolute values as in the original; erosion is separable, so the area limit is gone. |
| 229 | GetBitmapBits | P | P | Zero capacity is now a size query. Open: DDB row order may be inverted, see below. |
| 231 | GetBkColor | R | R | No defect found. |
| 232 | GetBkMode | R | R | No defect found. |
| 238 | GetCharABCWidths | P | R | Was a failure for non-TrueType fonts. Now the original result: A=0, B=advance, C=0. |
| 239 | GetCharWidth | R | R | No defect found. |
| 247 | GetClipRgn | P | R | Region handle 0 returns 0 without error, as in the original. |
| 254 | GetCurrentObject | R | R | No defect found. |
| 255 | GetCurrentPositionEx | R | R | No defect found. |
| 278 | GetEnhMetaFile | R | R | No defect found. |
| 279 | GetEnhMetaFileBits | R | R | No defect found. |
| 280 | GetEnhMetaFileHeader | R | R | No defect found. |
| 295 | GetKerningPairs | P | R | NULL output returns the total for any capacity. |
| 315 | GetMetaFile | R | R | No defect found. |
| 330 | GetOutlineTextMetrics | P | P | No longer limited to TrueType; capacity 0 fails 87; success returns 1. Limit: subscript and superscript fields are zero, PANOSE is synthesized, NULL output returns the real size rather than 0x116. |
| 336 | GetPolyFillMode | R | R | No defect found. |
| 360 | GetSystemPaletteEntries | P | P | Limit: returns the 20-entry logical system palette, not a hardware palette. |
| 367 | GetTextColor | R | R | No defect found. |
| 378 | GetViewportExtEx | R | R | No defect found. |
| 379 | GetViewportOrgEx | R | R | No defect found. |
| 381 | GetWinMetaFileBits | P | P | Mapping mode is now applied to the recording. Limit: conversion covers only what 515 can play and 83 can record. |
| 383 | GetWindowDC | P | P | Limit: PM-hosted frame chrome is not a drawable client bitmap. |
| 384 | GetWindowExtEx | R | R | No defect found. |
| 386 | GetWindowOrgEx | R | R | No defect found. |
| 455 | LineDDA | R | R | No defect found. |
| 499 | OffsetRgn | R | R | No defect found. |
| 500 | OffsetViewportOrgEx | R | R | No defect found. |
| 501 | OffsetWindowOrgEx | R | R | No defect found. |
| 510 | PaintRgn | R | R | No defect found. |
| 514 | Pie | R | R | No defect found. |
| 515 | PlayEnhMetaFile | P | P | Added 16-bit poly records, Bezier, PolylineTo, clip, palette, region, BitBlt/StretchBlt, SetDIBitsToDevice, DIB pattern brush, ExtCreatePen, Chord, CloseFigure, FillPath, StrokeAndFillPath; original no-op records accepted; degenerate bounds fail 87. Limit: types 41, 53, 55, 56, 66, 67, 78, 92, 93 and non-identity world transforms fail with error 50; fitted playback ignores recorded viewport and map-mode records rather than composing them. |
| 516 | PlayEnhMetaFileRecord | P | P | Same record set and limits as 515. |
| 522 | PolyPolygon | R | R | No defect found. |
| 523 | PolyPolyline | R | R | No defect found. |
| 532 | PtInRegion | R | R | No defect found. |
| 537 | RectInRegion | R | R | No defect found. |
| 538 | RectVisible | R | R | No defect found. |
| 569 | ResetDC | P | P | Information DCs accepted. Limit: virtual queues expose one A4/300 dpi mode, so nothing changes in the device. |
| 571 | ResizePalette | R | R | No defect found. |
| 575 | RoundRect | R | R | No defect found. |
| 577 | ScaleViewportExtEx | P | R | Now the original rule: no change outside modes 7/8 or with a zero factor; rounds. |
| 578 | ScaleWindowExtEx | P | R | Zero numerators fail 87; rounds. |
| 580 | ScrollDC | P | P | Rectangles now map through the DC. Limit: pending update regions are not scrolled. |
| 581 | ScrollWindow | P | P | Children moved even with a scroll rectangle; they now move only for a NULL rectangle. Limit as 580. |
| 582 | ScrollWindowEx | P | P | Unknown flags, including SW_SMOOTHSCROLL, are ignored as in the original instead of failing. Limit as 580. |
| 591 | SetBitmapBits | P | P | Zero count now fails 87. Open: DDB row order, as 229. |
| 609 | SetDIBitsToDevice | P | P | Clips to the supplied scan band instead of failing, clamps width, returns the line count. Limit: uncompressed RGB or palette-index DIBs only. |
| 614 | SetEnhMetaFileBits | R | R | No defect found. |
| 632 | SetPaletteEntries | P | R | NULL input and count 0 follow the original. |
| 634 | SetPixel | R | R | No defect found. |
| 635 | SetPolyFillMode | R | R | No defect found. |
| 644 | SetStretchBltMode | P | R | Modes above 3 now return 0 as in the original. HALFTONE filter removed. |
| 648 | SetTextCharacterExtra | R | R | No defect found. |
| 650 | SetTextJustification | R | R | No defect found. |
| 657 | SetWinMetaFileBits | P | P | rclBounds and rclFrame now derive from the WMF window or METAFILEPICT, so target rectangles scale. Limit: WMF stays embedded and plays at the comment; the original replays it as EMF records. |
| 676 | StretchBlt | P | P | Zero sizes return TRUE; NULL source routes to PatBlt. Limit: uncompressed DIB formats only. |
| 679 | StrokePath | R | R | No defect found. |
| 686 | TabbedTextOut | P | P | Text under mapping modes no longer fails; count -1 and NULL tabs work. Limit: glyphs keep device size and tab positions use device extents. |
| 704 | UnrealizeObject | P | R | Returns TRUE for every handle, as in the original instructions. |
| 1000 | WindowFromDC | P | R | Returns the public window. |
| 1004 | DeviceCapabilities | R | R | No defect found. |
| 1022 | HPSToHDC | P | P | Rectangle applied only with a driver name, as in the original. Limit: origin only, no driver extents. |
| 1023 | DeleteHDCObjects | P | R | Deleted the caller objects on ordinary DCs; it now acts only on HPSToHDC wrappers. |
| 1031 | SetAbortProc | F | R | Was stored and never called. open32AbortCheck now runs at StartDoc, StartPage, EndPage and EndDoc. |
| 5000 | _CreateBitmapIndirect | P | P | Follows the original checks; zero size gives 1x1 mono; wider pitch is repacked. Limit: one plane, depths 1/4/8/24/32. Open: row order, as 229. |
| 6004 | WinTranslateDeviceRects | P | R | Direction ignored as in the original; both handles 0 returns 0 without error. |
| 6005 | WinTranslateGraphicsObjectHandle | P | P | Direction is a byte, as in the original. Limit: GPI metafiles and WMF do not translate; converted palettes and regions are new objects with no cached association. |
