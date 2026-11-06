# Native PM export review (worker review-pm)

Semantic review of 116 rows: 93 native PM rows (PMWIN 34, PMGPI 57, PMCTLS 2)
plus MMIO #28/#29/#30/#77, PMDRAG (15), PMSPL #441, PMSHAPI #121/#128 and
SO32DLL #4. Everything here is **unbuilt and untested**. Classifications come
from reading source against the evidence below; none is a runtime result.

## Evidence

- Original decompilations: `.cache/os2-re/lotus-export-batch-2026-11-06/evidence/root-decompile/`.
- PMGPI SHA-256 `e9dea279178784ce62cfd4ae566974dcbebd10735e640be798653781189a28b3`.
  Most PMGPI exports are client thunks into GRE, so the exports alone rarely fix
  engine behaviour.
- Toolkit reference text from `sdk/os2tk45-master/book/gpi*.inf` and `pm*.inf`,
  read with a scratch slot extractor. This is the authority for much engine
  behaviour below, and it is documentation rather than instruction evidence.
- Extra Ghidra decompilation of internal helpers: PMGPI `FUN_1be81140`,
  `FUN_1be871d8`, `FUN_1be86ba4`; PMMERGE `FUN_1e98394c`, `FUN_1e983a54`,
  `FUN_1e98e350`, `FUN_1e93cfe8`, `FUN_1e95f824`, `FUN_1e97c1ac`, `FUN_1e9623a4`,
  `FUN_1e94f164`, `FUN_1e938f0c`. Provisional compiler model.
- PMSHAPI#128 (`export_5296`) pseudocode is bad-instruction output and establishes
  nothing about the body.

## Link closure

Checked against `LX_PRIVATE` names in `os2libs/pmwin/*.h` (80 names): no call from
pmgpi, pmctls, pmdrag, pmspl, pmshapi, mmio or so32dll, and no use of
`desktop_window`. PMWIN makes no call into Gpi/Dev/Drg/Spl/mmio. Existing link lines
cover the rows: pmgpi, pmctls, pmdrag and pmshapi link pmwin; pmspl links pmshapi;
every module links 2ine.

## Classification

Rows: 116. Before: REAL 39, PARTIAL 65, FAKE 12. After: REAL 77, PARTIAL 33, FAKE 6.

| Row | Symbol | Before | After | Finding | Repair or remaining limit |
|---|---|---|---|---|---|
| PMCTLS#2 | WinFontDlg | PARTIAL | pending | Dialog runs on the host catalogue; no style controls, FIXEDWIDTHONLY/PROPORTIONALONLY ignored |  |
| PMCTLS#3 | WinDefFontDlgProc | PARTIAL | pending | Handles OK/Cancel/Apply/Reset only; FNTM_* notifications from the original absent |  |
| PMGPI#352 | GpiBeginArea | PARTIAL | REAL | Recording real; BA_INCL/BA_EXCL ignored by the fill | Fill honours BA_INCL/EXCL |
| PMGPI#354 | GpiBeginPath | PARTIAL | REAL | Accepted any id; reference requires 1 | id must be 1 |
| PMGPI#360 | GpiCloseFigure | REAL | REAL | Closes current figure and moves position | unchanged |
| PMGPI#365 | GpiCopyMetaFile | REAL | REAL | Independent byte copy | unchanged |
| PMGPI#385 | GpiEndArea | PARTIAL | REAL | Boundary drawn with geometric stroke, should be cosmetic; area path leaked | Cosmetic closed boundary; area path freed |
| PMGPI#387 | GpiEndPath | REAL | REAL | Ends bracket | unchanged |
| PMGPI#392 | GpiFillPath | PARTIAL | REAL | Path not deleted after use; FPATH_INCL/EXCL ignored | id 1 only; path deleted after use; FPATH_INCL/EXCL applied |
| PMGPI#393 | GpiFullArc | REAL | REAL | Uses arc parameters and multiplier | Hits propagated; failed bracket freed |
| PMGPI#409 | GpiPaintRegion | PARTIAL | REAL | Fills with area attributes; accepted the selected clip region | Rejects selected clip region; hit result |
| PMGPI#412 | GpiPointArc | PARTIAL | REAL | Ignored arc parameters | Circle solved in arc-parameter space, ends exactly on the end point |
| PMGPI#419 | GpiPtInRegion | PARTIAL | REAL | Correct test; accepted the selected clip region | Rejects selected clip region |
| PMGPI#443 | GpiQueryDefaultViewMatrix | PARTIAL | REAL | Accepted NULL with count 0; original rejects | NULL rejected, zero count allowed by bridge |
| PMGPI#454 | GpiQueryGraphicsField | PARTIAL | REAL | Unset state returned the page rectangle | Unset returns zero rectangle |
| PMGPI#457 | GpiQueryLineEnd | REAL | REAL | Reads state consumed by stroke | unchanged |
| PMGPI#458 | GpiQueryLineJoin | REAL | REAL | Reads state consumed by stroke | unchanged |
| PMGPI#461 | GpiQueryLineWidthGeom | REAL | REAL | Reads state consumed by stroke | unchanged |
| PMGPI#465 | GpiQueryMetaFileBits | REAL | REAL | Checked range copy | unchanged |
| PMGPI#466 | GpiQueryMetaFileLength | REAL | REAL | Matches original | unchanged |
| PMGPI#468 | GpiQueryModelTransformMatrix | PARTIAL | REAL | Accepted NULL with count 0 | NULL rejected, zero count allowed by bridge |
| PMGPI#469 | GpiQueryNearestColor | PARTIAL | REAL | Rejected LCOLOPT_REALIZED | LCOLOPT_REALIZED accepted |
| PMGPI#471 | GpiQueryPS | REAL | REAL | Returns page size and options | unchanged |
| PMGPI#476 | GpiQueryPel | PARTIAL | REAL | Host pixel not masked to 24 bits before index lookup | Masked; clipped pel errors; CLR_NOINDEX when absent from table |
| PMGPI#477 | GpiQueryPickAperturePosition | FAKE | REAL | Returned state nothing consumes | Now feeds correlation hit test |
| PMGPI#478 | GpiQueryPickApertureSize | FAKE | REAL | Returned state nothing consumes | Now feeds correlation hit test |
| PMGPI#480 | GpiQueryRealColors | PARTIAL | PARTIAL | LCOLOPT_INDEX pairs missing; count above 256 rejected | LCOLOPT_INDEX pairs added. Limit: reports the 256-entry logical table, not a device palette |
| PMGPI#490 | GpiQueryViewingLimits | PARTIAL | REAL | Unset state returned page rectangle, not no-clipping limits | Unset returns GRE no-clipping rectangle |
| PMGPI#494 | GpiRectInRegion | PARTIAL | REAL | Correct test; accepted the selected clip region | Rejects selected clip region |
| PMGPI#497 | GpiResetBoundaryData | PARTIAL | REAL | Reset correct, but DCTL_BOUNDARY mapped wrongly so data never accumulated | DCTL_BOUNDARY now enables accumulation |
| PMGPI#498 | GpiResetPS | PARTIAL | REAL | Failed in open bracket; GRES_ATTRS over-reset; GRES_ALL effects missing | Aborts open bracket; ATTRS/SEGMENTS/ALL effects per reference |
| PMGPI#500 | GpiSaveMetaFile | PARTIAL | REAL | Overwrote existing file; handle stayed valid | Exclusive create; handle closed on success |
| PMGPI#502 | GpiSetArcParams | PARTIAL | REAL | Applied to full/partial arcs, not point arcs | Now read by point arcs too |
| PMGPI#526 | GpiSetGraphicsField | PARTIAL | REAL | Applied as device units with exclusive edges; reference says page units, inclusive | Page units, inclusive edges, PMERR_INV_GRAPHICS_FIELD |
| PMGPI#528 | GpiSetLineEnd | REAL | REAL | Applied by stroke | unchanged |
| PMGPI#529 | GpiSetLineJoin | REAL | REAL | Applied by stroke | unchanged |
| PMGPI#532 | GpiSetLineWidthGeom | PARTIAL | REAL | Applied by stroke, but stroke filled with line attributes and an unsupported 32767 cap | Any non-negative width; rejected in bracket; stroke uses area attributes |
| PMGPI#538 | GpiSetModelTransformMatrix | PARTIAL | REAL | TRANSFORM_ADD and TRANSFORM_PREEMPT order swapped | ADD/PREEMPT order corrected; existing matrix validated |
| PMGPI#539 | GpiSetPS | PARTIAL | PARTIAL | Rejected valid PS_FORMAT; reset weaker than original | PS_FORMAT accepted; full reset unless PS_NORESET. Limit: format has no effect, no segment store |
| PMGPI#545 | GpiSetPickAperturePosition | FAKE | REAL | Stored, never read by any drawing path | Read by DCTL_CORRELATE hit test |
| PMGPI#550 | GpiSetStopDraw | FAKE | PARTIAL | Blocked all drawing; reference says no effect on other calls | No longer blocks drawing. Limit: no retained drawing exists here to terminate, so nothing reads the condition |
| PMGPI#552 | GpiSetViewingLimits | PARTIAL | REAL | Applied with exclusive edges; reference inclusive | Inclusive edges; custom state tracked |
| PMGPI#554 | GpiStrokePath | PARTIAL | REAL | Geometric width applied, but with line attributes and path not deleted | Area attributes; deleted after use; device-width thin path |
| PMGPI#559 | GpiPathToRegion | PARTIAL | REAL | Path not deleted; FPATH_INCL/EXCL ignored | id 1 only; deleted after use; INCL/EXCL |
| PMGPI#564 | GpiTranslate | PARTIAL | REAL | ADD order swapped; PREEMPT accepted, original rejects | ADD order fixed; PREEMPT rejected per FUN_1be81140 |
| PMGPI#565 | GpiScale | PARTIAL | REAL | ADD order swapped; PREEMPT accepted, original rejects | ADD order fixed; PREEMPT rejected per FUN_1be81140 |
| PMGPI#566 | GpiRotate | PARTIAL | REAL | ADD order swapped; PREEMPT accepted, original rejects | ADD order fixed; PREEMPT rejected per FUN_1be81140 |
| PMGPI#569 | GpiQueryDefViewingLimits | PARTIAL | REAL | Unset state wrong | Unset returns no-clipping rectangle |
| PMGPI#572 | GpiSetDefViewingLimits | FAKE | REAL | Stored, never applied to drawing | Applied at once to default limits; used by reset and association |
| PMGPI#577 | GpiDeletePalette | PARTIAL | REAL | Refused while selected, no error code | PMERR_PALETTE_SELECTED / PMERR_INV_HPAL set |
| PMGPI#578 | GpiSelectPalette | PARTIAL | REAL | Lease correct; RGB mode kept and palette ignored | Overrides RGB mode and loaded table; NULL restores default |
| PMGPI#579 | GpiQueryPalette | REAL | REAL | Returns selected handle | unchanged |
| PMGPI#583 | GpiQueryAttrs | PARTIAL | REAL | Returned 1, not the defaults mask; marker/image sizes wrong in bridge | Returns defaults mask; bundle sizes corrected |
| PMGPI#589 | GpiSetPickApertureSize | FAKE | PARTIAL | Stored only; PICKAP_DEFAULT with NULL failed | Read by hit test; PICKAP_DEFAULT with NULL works; default size 1x1 is an assumption |
| PMGPI#594 | GpiCreatePalette | PARTIAL | REAL | Rejected valid LCOL_PURECOLOR/OVERRIDE; accepted reserved bit 1 | Options and format per reference |
| PMGPI#597 | GpiQueryPaletteInfo | PARTIAL | REAL | LCOLOPT_INDEX missing; no-palette case returned 16, not 0 | LCOLOPT_INDEX pairs; 0 with no palette |
| PMGPI#612 | GpiPartialArc | REAL | REAL | Line to start, then arc | Hits propagated |
| PMGPI#614 | GpiSetCharExtra | PARTIAL | PARTIAL | Applied, but negative rounding wrong and world units not converted | Rounding and world-to-device conversion fixed; GpiQueryTextBox includes it. Limit: WinDrawText line measurement does not |
| PMGPI#650 | GpiPolygons | PARTIAL | REAL | First polygon ignored current position; NULL array accepted | First polygon starts at current position; NULL rejected; cosmetic boundary |
| PMWIN#705 | WinCancelShutdown | FAKE | pending | Stored, nothing read it |  |
| PMWIN#712 | WinCpTranslateString | PARTIAL | pending | Translation real; error order and codes differ from original |  |
| PMWIN#722 | WinDeleteLibrary | PARTIAL | pending | Frees module while borrowed; review of ownership needed |  |
| PMWIN#745 | WinFlashWindow | PARTIAL | pending | Original sends WM_FLASHWINDOW; body set state directly |  |
| PMWIN#746 | WinFocusChange | FAKE | pending | Flags discarded; reduced to WinSetFocus |  |
| PMWIN#748 | WinFreeErrorInfo | REAL | pending | Releases tracked block |  |
| PMWIN#751 | WinGetErrorInfo | PARTIAL | pending | Header offsets zero; original sets offaoffszMsg and offBinaryData |  |
| PMWIN#754 | WinGetMaxPosition | PARTIAL | pending | No border overhang; set SWP_MAXIMIZE, original ORs SWP_SIZE|SWP_MOVE only |  |
| PMWIN#755 | WinGetMinPosition | FAKE | pending | Input pointer treated as output; no slot search |  |
| PMWIN#766 | WinInvalidateRegion | REAL | pending | Per-rectangle invalidation |  |
| PMWIN#774 | WinIsWindowShowing | PARTIAL | pending | Ancestor styles only; original tests visible region |  |
| PMWIN#777 | WinLoadLibrary | REAL | pending | Loader-backed |  |
| PMWIN#779 | WinLoadMessage | FAKE | pending | Read resource id directly; no 16-string bundle decode |  |
| PMWIN#791 | WinNextChar | PARTIAL | pending | Invented code page failure; reference falls back to queue code page |  |
| PMWIN#802 | WinQueryAtomName | REAL | pending | Named and integer atoms |  |
| PMWIN#814 | WinQueryDlgItemShort | PARTIAL | pending | Output not cleared; NULL output failed; parsing differs from original |  |
| PMWIN#818 | WinQueryMsgPos | REAL | pending | Reads position kept by message retrieval |  |
| PMWIN#820 | WinQueryObjectWindow | REAL | pending | Returns object window |  |
| PMWIN#822 | WinQueryPointerInfo | PARTIAL | pending | Host-only system cursors fail (stated limit) |  |
| PMWIN#824 | WinQueryQueueInfo | PARTIAL | pending | Short buffer and NULL failed; original returns TRUE |  |
| PMWIN#832 | WinQueryUpdateRegion | PARTIAL | pending | Region copy real; selected clip region accepted |  |
| PMWIN#857 | WinSetCp | PARTIAL | pending | cp 0 rejected; original uses process code page |  |
| PMWIN#858 | WinSetDlgItemShort | REAL | pending | Matches original formatting |  |
| PMWIN#863 | WinSetMultWindowPos | PARTIAL | pending | No fl range check; sequential rather than batched |  |
| PMWIN#871 | WinSetSysColors | PARTIAL | pending | LCOL_RESET ignored |  |
| PMWIN#873 | WinSetSysValue | PARTIAL | pending | Index set differs from original; border clamp absent |  |
| PMWIN#874 | WinSetWindowBits | REAL | pending | Read-modify-write |  |
| PMWIN#890 | WinTrackRect | FAKE | pending | No rectangle drawn; VK codes wrong (9=SHIFT, 13=PAUSE) |  |
| PMWIN#895 | WinValidateRect | REAL | pending | Region subtraction with children |  |
| PMWIN#896 | WinValidateRegion | REAL | pending | Region subtraction with children |  |
| PMWIN#901 | WinBroadcastMsg | PARTIAL | pending | Owned by os2-bb; flags differ from original, not rewritten here |  |
| PMWIN#921 | WinSetKeyboardStateTable | REAL | pending | Copies state both ways |  |
| PMWIN#941 | WinRealizePalette | REAL | pending | True-colour surface has nothing to realize |  |
| PMWIN#942 | WinCreatePointerIndirect | REAL | pending | Builds image from bitmaps |  |
| MMIO#28 | mmioRead | REAL | pending | MEM element read; EOF returns 0 |  |
| MMIO#29 | mmioWrite | FAKE | pending | Open rejected write modes and writes never reached the caller buffer |  |
| MMIO#30 | mmioSeek | REAL | pending | Three origins, bounded |  |
| MMIO#77 | mmioGetHeader | REAL | pending | Mirrors MEM IOProc -2 path |  |
| PMDRAG#32 | DrgAccessDraginfo | REAL | pending | Reference counted |  |
| PMDRAG#34 | DrgAllocDraginfo | REAL | pending | Layout matches original offsets |  |
| PMDRAG#35 | DrgAllocDragtransfer | PARTIAL | pending | Elements not individually freeable |  |
| PMDRAG#36 | DrgDeleteDraginfoStrHandles | PARTIAL | pending | Shared handles failed; rewrote items, original does not |  |
| PMDRAG#38 | DrgDrag | PARTIAL | pending | Transaction real; DM_DRAGOVER lacked position; images never shown |  |
| PMDRAG#39 | DrgFreeDraginfo | REAL | pending | Reference counted |  |
| PMDRAG#40 | DrgFreeDragtransfer | PARTIAL | pending | Only base address accepted; no reference for transfer messages |  |
| PMDRAG#41 | DrgGetPS | REAL | pending | Window PS |  |
| PMDRAG#42 | DrgPostTransferMsg | PARTIAL | pending | fsReply not cleared; no transfer reference |  |
| PMDRAG#45 | DrgQueryDragitemCount | REAL | pending | Matches original |  |
| PMDRAG#46 | DrgQueryDragitemPtr | REAL | pending | Matches original |  |
| PMDRAG#53 | DrgReleasePS | REAL | pending | Releases PS |  |
| PMDRAG#54 | DrgSendTransferMsg | PARTIAL | pending | fsReply not cleared; no transfer reference |  |
| PMDRAG#57 | DrgSetDragitem | PARTIAL | pending | Short cb failed; original copies min(cb,36) |  |
| PMDRAG#59 | DrgVerifyRMF | REAL | pending | Both RMF forms parsed |  |
| PMSPL#441 | SplEnumPrinter | PARTIAL | pending | Level checked after type; original checks level first |  |
| PMSHAPI#121 | WinCreateSwitchEntry | PARTIAL | pending | No idProcess/idSession 16-bit range check |  |
| PMSHAPI#128 | WinQueryTaskTitle | PARTIAL | pending | Original body not recoverable from bad-instruction pseudocode |  |
| SO32DLL#4 | Os2LegacyGetHostId | REAL | pending | Same logic as TCPIP32 gethostid |  |
