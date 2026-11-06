# Open32 services review (127 PMWINX rows), 6 November 2026

Worker `review-services`. **Unbuilt.** Nothing here has been compiled, linked or
run. REAL means the body does the row's job on reading, not that it was
verified.

Original: `extracted/install-media/os2image/FI/DAX/DLL/PMWINX.DLL`, SHA-256
`07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`. Object 1 is
based at 0x10000, so addresses below are linked addresses.

## Method and how far it reaches

The retained Ghidra pseudocode is unreliable here: it drops import calls and
shows the `_System` AL argument count as a constant (`return 3`, `CONCAT31`). The
findings come from instructions instead, using a throwaway script that unpacked
EXEPACK2 pages, applied LX fixups, walked chained internal fixups and fed
`ndisasm`. Chained fixups matter: `lx-fixups` lists only chain heads, and a raw
operand such as `0x35204670` is next-offset 0x352 with target 0x04670.

Rows with an address in the finding were compared with those instructions. Rows
with a blank finding had their body and bridge read and showed no defect, but
were not compared instruction by instruction. Interlocked and LoadMenu belong to
os2-bb and were not reviewed.

Bridges: all 120 in `pmwinx_batch_services.h` match the policy `argument_bytes`.
The other seven live elsewhere (LoadMenu, Interlocked, SetCursorPos,
GetMenuCheckMarkDimensions, MultiByteToWideChar, WideCharToMultiByte).
`batch_services.h` agrees with the merged `os2libs/pmwinx.h` prototypes. A
scripted scan found no callee without a declaration and body, and no `LX_PRIVATE`
symbol used from `os2libs/pmwinx/` or its bindings. The scans are regular
expressions, not a compiler.

## Classification

Before repairs: 66 REAL, 51 PARTIAL, 10 FAKE. After: 91 REAL, 34 PARTIAL, 2 FAKE.

| Ord | Row | Before | After | Finding |
|---|---|---|---|---|
| 3 | AddAtom | REAL | REAL | Process-local table with reference counts; integer and #n atoms. |
| 5 | AdjustWindowRect | REAL | REAL | Calls AdjustWindowRectEx(...,0) as the original; the callee is os2-bb work in adjust_window.c. |
| 14 | Beep | PARTIAL | REAL | Added frequency -1 -> 50 and 395 -> 87 from 0x71700. |
| 15 | BeginDeferWindowPos | REAL | REAL | Batch handle; cleanup now hooked. |
| 34 | CheckRadioButton | PARTIAL | REAL | first>last was error 87; original simply does nothing and returns TRUE. |
| 35 | ChildWindowFromPoint | PARTIAL | REAL | Now WinWindowFromPoint(client,TRUE) with y=yTop-y as 0xa04bc; hidden children no longer match. |
| 36 | ChooseColor | PARTIAL | PARTIAL | Honours CC_RGBINIT now. Hex entry dialog, not the original colour grid; hooks, templates and custom colours rejected. |
| 37 | ChooseFont | PARTIAL | PARTIAL | CF_INITTOLOGFONTSTRUCT test was inverted, fixed. Hook/template modes rejected. |
| 40 | ClipCursor | PARTIAL | PARTIAL | Was thread-local and only clamped programmatic moves. Now process-wide PM clip applied to host motion events with original bounds arithmetic. Limit: a released-capture window cannot warp the host pointer. |
| 49 | CompareFileTime | REAL | REAL | Matches 0x9d654. |
| 52 | CopyFile | REAL | REAL | DosCopy, DCPY_EXISTING unless exclusive. |
| 67 | CreateDialog | PARTIAL | PARTIAL | Numeric resources only; string names rejected. |
| 68 | CreateDialogParam | PARTIAL | PARTIAL | Numeric resources only; string names rejected. |
| 69 | CreateDirectory | PARTIAL | REAL | Rejected security attributes; original ignores them. |
| 73 | CreateEvent | FAKE | PARTIAL | Handles could be neither waited on nor closed until hooks landed. Security rejection removed and 183-only error as 0x962e0. Limit: handles are not inherited. |
| 84 | CreateMutex | FAKE | PARTIAL | As CreateEvent. |
| 101 | DdeAbandonTransaction | PARTIAL | PARTIAL | No async transactions exist, so only the validating no-op applies. |
| 102 | DdeAccessData | REAL | REAL | Local data handle. |
| 103 | DdeAddData | REAL | REAL | Local data handle. |
| 104 | DdeClientTransaction | PARTIAL | PARTIAL | Same-thread servers only; TIMEOUT_ASYNC refused. Original forwards to PMDDEML, which 2ine lacks. |
| 106 | DdeConnect | PARTIAL | PARTIAL | Same-thread servers only. |
| 108 | DdeCreateDataHandle | REAL | REAL | Local data handle. |
| 110 | DdeDisconnect | PARTIAL | PARTIAL | Same-thread conversations only. |
| 113 | DdeFreeDataHandle | REAL | REAL | Local data handle. |
| 115 | DdeGetData | REAL | REAL | Local data handle. |
| 120 | DdePostAdvise | PARTIAL | PARTIAL | Same-thread conversations only. |
| 121 | DdeQueryConvInfo | PARTIAL | PARTIAL | CONVINFO layout correct at 100 bytes; partner fields are synthetic. |
| 125 | DdeSetUserHandle | PARTIAL | PARTIAL | QID_SYNC only, as no async transactions exist. |
| 126 | DdeUnaccessData | REAL | REAL | Local data handle. |
| 128 | DefDlgProc | REAL | REAL | Routes command/close to WinDefDlgProc. |
| 132 | DeferWindowPos | REAL | REAL | Applied at End. |
| 133 | DeleteAtom | REAL | REAL |  |
| 137 | DeleteFile | REAL | REAL | DosDelete. |
| 153 | DlgDirSelectEx | REAL | REAL | List box text with bracket parsing. |
| 154 | DosDateTimeToFileTime | REAL | REAL |  |
| 155 | DragAcceptFiles | FAKE | FAKE | NOT REPAIRED. Stores WS_EX_ACCEPTFILES, but nothing in PMWINX, PMWIN or the SDL HAL produces WM_DROPFILES. |
| 156 | DragFinish | REAL | REAL | Frees the HGLOBAL. |
| 157 | DragQueryFile | PARTIAL | PARTIAL | Parses DROPFILES correctly; only guest-built handles can reach it. |
| 158 | DragQueryPoint | PARTIAL | PARTIAL | As DragQueryFile. |
| 169 | EndDeferWindowPos | REAL | REAL | Sequential SetWindowPos, not one WinSetMultWindowPos. |
| 185 | EnumThreadWindows | PARTIAL | PARTIAL | Current anchor only. |
| 186 | EnumWindows | PARTIAL | PARTIAL | Current anchor only; original also walks HWND_OBJECT in a second pass at 0x9fc84. |
| 203 | FileTimeToDosDateTime | REAL | REAL |  |
| 209 | FindAtom | REAL | REAL |  |
| 215 | FindWindow | PARTIAL | PARTIAL | NULL class now 87 and title limited to 100 bytes as 0xa3104. Current anchor only. |
| 261 | GetCursor | REAL | REAL |  |
| 271 | GetDiskFreeSpace | PARTIAL | REAL | Over-strict root parse removed; first letter selects the drive as 0x72e0c. |
| 272 | GetDlgCtrlID | REAL | REAL |  |
| 274 | GetDlgItemInt | REAL | REAL |  |
| 276 | GetDoubleClickTime | REAL | REAL |  |
| 282 | GetEnvironmentStrings | REAL | REAL |  |
| 283 | GetEnvironmentVariable | PARTIAL | REAL | NULL output or zero capacity is now 87 as 0x96428. |
| 287 | GetFileTime | PARTIAL | PARTIAL | Creation time now read (macOS birth time; Linux xattr then statx). One-second precision; zero when the host has none. |
| 288 | GetFileTitle | REAL | REAL | Bridge span now uses the 16-bit capacity. |
| 291 | GetForegroundWindow | REAL | REAL |  |
| 296 | GetKeyNameText | PARTIAL | PARTIAL | English names, US scan map. |
| 299 | GetLastActivePopup | PARTIAL | REAL | Rewritten: original walks z-order for the window or its owned popup (0xa4008); old code followed owners from the active window. |
| 306 | GetMenuCheckMarkDimensions | REAL | REAL |  |
| 323 | GetNextDlgTabItem | REAL | REAL |  |
| 324 | GetNextWindow | PARTIAL | REAL | Invalid direction sets no error; NULL means desktop. The class #32765 skip at 0x9fdb8 has no 2ine equivalent; the receiver is an object window instead. |
| 338 | GetPriorityClipboardFormat | REAL | REAL |  |
| 339 | GetPrivateProfileInt | REAL | REAL |  |
| 357 | GetSystemDirectory | PARTIAL | REAL | NULL output returns 0 as 0x9c200. |
| 363 | GetTempFileName | PARTIAL | REAL | Name format was lower-case .tmp, now %3.3s%04X.TMP; retries on 110 as well as 80. |
| 364 | GetTempPath | PARTIAL | REAL | Order was TMP, TEMP, Windows directory; original is TEMP, TMP, current directory. |
| 374 | GetTimeZoneInformation | PARTIAL | PARTIAL | Bias only, TIME_ZONE_ID_UNKNOWN. Original parses TZ rules. |
| 375 | GetTopWindow | REAL | REAL | Receiver moved under HWND_OBJECT so it cannot appear here. |
| 377 | GetUpdateRgn | REAL | REAL | Uses exported lx_pm_window_damage (regions.c link defect fixed by another session). |
| 392 | GetWindowWord | REAL | REAL |  |
| 405 | GlobalMemoryStatus | PARTIAL | REAL | Fields were wrong; now the 0x1ff34 arithmetic, including its unsigned paging subtraction. |
| 421 | InterlockedDecrement | REAL | REAL | os2-bb. |
| 423 | InterlockedIncrement | REAL | REAL | os2-bb. |
| 431 | IsBadHugeReadPtr | REAL | REAL |  |
| 432 | IsBadHugeWritePtr | REAL | REAL |  |
| 434 | IsBadStringPtr | PARTIAL | REAL | Now sets error 5 as 0x1fe78. |
| 436 | IsCharAlpha | REAL | REAL |  |
| 437 | IsCharAlphaNumeric | REAL | REAL |  |
| 439 | IsCharUpper | REAL | REAL |  |
| 440 | IsChild | PARTIAL | REAL | NULL parent means desktop as 0x9f628. |
| 443 | IsDialogMessage | REAL | REAL |  |
| 462 | LoadMenu | REAL | REAL | os2-bb. |
| 477 | LockWindowUpdate | PARTIAL | PARTIAL | Was per-thread; now process-wide as WinLockWindowUpdate. Native GPI calls that bypass Open32 DC lookup are unclipped. |
| 483 | MapDialogRect | REAL | REAL |  |
| 484 | MapWindowPoints | PARTIAL | REAL | Zero count is 87 as 0x8a8c8. |
| 487 | MessageBox | PARTIAL | REAL | Button map was wrong (ABORTRETRYIGNORE showed YESNOCANCEL). Tables decoded at 0x6b510; MB_MOVEABLE, system modal and NULL owner added. The SC_CLOSE removal for yes/no boxes is not reproduced. |
| 488 | ModifyMenu | REAL | REAL |  |
| 490 | MoveFile | REAL | REAL |  |
| 508 | OutputDebugString | REAL | REAL | stderr in place of PMMERGE.5006. |
| 529 | PostThreadMessage | FAKE | REAL | No queue was ever registered and the receiver packet was dispatched to the static control and lost. Both fixed; unknown thread is 87. |
| 551 | RegQueryInfoKey | REAL | REAL |  |
| 552 | RegQueryValue | REAL | REAL |  |
| 554 | RegSetValue | REAL | REAL |  |
| 561 | ReleaseMutex | FAKE | REAL | Unusable until waits were wired. |
| 563 | RemoveDirectory | REAL | REAL |  |
| 564 | RemoveFontResource | FAKE | FAKE | NOT REPAIRED. GpiUnloadFonts exists nowhere in 2ine, so it always fails with error 50. GpiLoadFonts is also absent and neither is a frozen row. |
| 568 | ReplyMessage | PARTIAL | PARTIAL | FALSE is correct here: PM has no interthread send in 2ine. |
| 570 | ResetEvent | FAKE | REAL | Unusable until waits were wired. |
| 583 | SearchPath | REAL | REAL |  |
| 606 | SetCursorPos | REAL | REAL | Clip now applied in PM as well. |
| 610 | SetDlgItemInt | REAL | REAL |  |
| 615 | SetEnvironmentVariable | REAL | REAL | Each call leaves its old block mapped for guests that hold the pointer. |
| 616 | SetEvent | FAKE | REAL | Unusable until waits were wired. |
| 617 | SetFileAttributes | PARTIAL | PARTIAL | Mask corrected to 0x1A7. DosSetPathInfo applies only read-only; hidden, system and archive are dropped. |
| 619 | SetFileTime | FAKE | PARTIAL | DOS handles reported success while DosSetFileInfo discarded all dates. DosSetFileInfo now applies them, creation writes exist, and image volumes take the write date. Limits in the list below. |
| 623 | SetHandleCount | FAKE | REAL | Was a no-op wrongly described as original; original calls DosSetRelMaxFH twice (0x719e4). |
| 664 | SetWindowWord | REAL | REAL |  |
| 668 | ShowCursor | REAL | REAL | Per-thread count as the original. |
| 696 | TrackPopupMenu | PARTIAL | REAL | Now modal until dismissed like 0xa0b98, flags 0x3C6, original placement, TPM_RETURNCMD ignored rather than refused. |
| 698 | TranslateMDISysAccel | REAL | REAL | Also accepts WM_SYSKEYDOWN. Fixed key logic rather than the original accelerator table. |
| 709 | VkKeyScan | PARTIAL | PARTIAL | US map. |
| 719 | WriteProfileString | REAL | REAL | WINI_ prefix confirmed. |
| 722 | ZeroMemory | PARTIAL | REAL | Bridge no longer refuses spans above 16 MiB. |
| 724 | _lcreat | PARTIAL | REAL | Mode was 0x42; now 0x12, or 0 for read-only, with attribute bits 1, 2, 4 (0x736d8). |
| 726 | _lopen | REAL | REAL |  |
| 1007 | MapVirtualKey | PARTIAL | PARTIAL | US map. |
| 1016 | GetMenuContextHelpId | REAL | REAL |  |
| 1017 | SetMenuContextHelpId | REAL | REAL |  |
| 1025 | ExtractIcon | PARTIAL | PARTIAL | OS/2 icon and LX module resources only. |
| 1033 | lstrcmp2 | PARTIAL | REAL | Was byte strcmp; now UTF-16 units from the OEM code page as 0x9e314, without lstrcmpW collation checks. |
| 2053 | GetKeyboardLayout | PARTIAL | PARTIAL | Fixed US layout. |
| 2057 | GetStringTypeEx | PARTIAL | PARTIAL | US/default locales, Latin classification. |
| 2073 | MultiByteToWideChar | REAL | REAL | Entry validation and bridge read; conversion core not reviewed. |
| 2079 | WideCharToMultiByte | REAL | REAL | Entry validation and bridge read; conversion core not reviewed. |
| 5002 | _CreateDialogIndirectParam | PARTIAL | PARTIAL | Win32 control styles were passed to PM unmapped; now mapped. Template font ignored, no DLGTEMPLATEEX. |
| 5004 | _DialogBoxIndirect | PARTIAL | PARTIAL | As _CreateDialogIndirectParam. |
| 5005 | _DialogBoxIndirectParam | PARTIAL | PARTIAL | As _CreateDialogIndirectParam. |
| 6011 | WinXTranslateMsg | REAL | REAL |  |

## Hook integration

| Hook | Call site |
|---|---|
| `open32CloseSync` | `CloseHandle` closer table, `files.c` |
| `open32WaitSync` | `WaitForSingleObject` waiter table, `threads.c` |
| `open32SyncCleanup`, `open32DeferredCleanup`, `open32DialogsCleanup` | `open32Shutdown`, `module.c` |
| `open32DeferredCleanup`, `open32DialogsCleanup`, `open32UnregisterThreadQueue` | new per-thread exit callback, `messages.c`, run first in PM `cleanupThreadPM` through `lx_pm_set_open32_thread_exit` |
| `open32RegisterThreadQueue` | `open32Presentation`, `context.c` |
| `open32UnregisterThreadQueue` | `open32ContextCleanup`, `context.c` |
| `open32ClipCursorPoint` | `SetCursorPos`, `pointers.c`; the clip itself is enforced in PM |
| `open32WindowUpdateLocked` | `open32FindDC` and the paint filter (already present) |

`CloseHandle` and `WaitForSingleObject` are tables so one entry adds a handle
type. Worker `process` had not created `os2libs/pmwinx/process.c` when this
review ended, so no process entry exists.

The thread receiver is now a child of `HWND_OBJECT`, keeping it out of
GetTopWindow, GetNextWindow and enumeration. `accept_native` previously dispatched
receiver packets into the static control, which would have lost every
PostThreadMessage even once registered.

## Queued control items from os2-90

1. `open32NativeDialogWindow` returns TRUE for `open32NativeControlOwned`. Done.
2. `WM_CONTROL` in `open32PmMessage` resolves the child and translates the code.
   Done. No double translation: `dialog_procedure` handles `WM_CONTROL` in its own
   branch and never reaches `open32PmMessage` for it.
3. `dialog_pm_style`: defect confirmed from `h/os2win.h` against `h/pmwin.h`
   (BS_CHECKBOX 2 against 1, BS_AUTORADIOBUTTON 9 against 4, CBS_DROPDOWNLIST 3
   against 4). Fixed by exporting `open32ControlStyle` from `native_controls.c`;
   no second table. A BS_GROUPBOX now also becomes a static control. The original
   PMWINX template converter was **not** located: a byte search for a style table
   and for the compare pattern found nothing, so this rests on the headers and not
   on original instructions.

## Remaining limits

- **FAKE, DragAcceptFiles:** no producer of WM_DROPFILES exists. It needs an SDL
  drop event, host-to-guest path mapping and HDROP construction.
- **FAKE, RemoveFontResource:** always error 50 until PMGPI gains GpiUnloadFonts.
- **DDE:** same-thread only, no async transactions, no PMDDEML.
- **Enumeration, FindWindow:** current PM anchor only; EnumWindows omits the
  original HWND_OBJECT pass.
- **ClipCursor:** a window whose capture is released cannot warp the host pointer,
  by the HAL's existing policy. Clamped coordinates are still delivered to the guest.
- **File times:** one-second precision. Linux keeps a guest-set creation time in
  `user.2ine.created` and fails on filesystems without xattrs; statx needs glibc
  2.28. SetFileTime reports error 50 there, while DosSetFileInfo skips only the
  creation date and succeeds, which is a deliberate compatibility choice.
- **Disk images:** the write date reaches FAT, HPFS and raw FAT entries. Access
  dates do not, and creation dates are refused. A date change needs a writable
  handle, so DosSetFileInfo with non-zero dates on a read-only image handle now
  fails where it previously reported success.
- **DosSetPathInfo:** still applies only the read-only bit. SetFileAttributes
  loses hidden, system and archive, and path-based dates stay unapplied.
- **Dialogs:** template fonts are ignored and DLGTEMPLATEEX is unsupported. It is
  likely absent from this NT 3.51-level API, but that was not checked in the
  original. `convert_dialog` was left alone beyond the style fix at the
  coordinator's request. CreateDialog takes numeric resources only.
- **MessageBox:** the original removes SC_CLOSE from yes/no boxes; this does not.
- **Common dialogs:** ChooseColor is a hex entry box; hooks and templates refused.
- **Other:** GetTimeZoneInformation bias only; keyboard rows use the US map;
  ReplyMessage is always FALSE; SetEnvironmentVariable retains old blocks.

## HAL coverage

Every host service used resolves on Linux and macOS through `hal/posix`, the two
platform directories or `hal/sdl`. `hal/platform.cmake` refuses Windows outright
because `hal/win` has no filesystem adapter, so `lx_host_file_times` and the new
`lx_host_file_created` and `lx_host_file_set_created` have no Windows body. That
gap predates this work.

## Next action

Coordinator's first build, then runtime checks of: named and auto-reset events
across two processes, PostThreadMessage to a second thread, TrackPopupMenu command
ordering, ClipCursor under real pointer motion, and FAT and HPFS date writes.
