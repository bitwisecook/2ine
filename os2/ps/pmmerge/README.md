<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# PMMERGE.DLL

The merged Presentation Manager library: the window manager (PMWIN), the
graphics engine entries (PMGRE), the shell API (PMSHAPI), the WC_* window
controls and the PMMERGE-only entries.

- IBM source: `E:\w45ps\src\ps\pmmerge`, linking `ps\pmwin`, `ps\pmgre`,
  `ps\pmgre\386`, `ps\ishapi`, `ps\controls\pmwctls` and `ps\common\src`.
- Evidence: SYM directory `ps/pmmerge`; `@BSS` labels in `pmmrggre.sym` and
  `PMMERGE.SYM`; BLDLEVEL "OS/2 PM Merged Library"; LX entry tables of
  PMMERGE.DLL (inventory `modules.json`, f1f56353dde3…) and of the PMWIN,
  PMGRE and PMSHAPI forwarders.
- In Warp 4.52: Real code; PMWIN, PMGRE and PMSHAPI forward to it.
- In 2ine: Implements every PMWIN, PMGRE and PMSHAPI export at the PMMERGE
  ordinal Warp's forwarder names, and WinQueryCpType. Other native DLLs
  (PMGPI, PMCTLS, PMWINX, PMWP, PMSPL, DIVE, MDM, OPENGL) link this library
  for the C entry points and for the private interfaces in
  `os2/include/private/pmp.h`.
- Exports: `pmmerge.def`.

## Units

The sub-directories follow IBM's source trees. A unit named after an IBM
unit is placed where that unit covers the same API area; every such match is
inferred from the IBM file name and does not claim that the code matches the
IBM unit. Units without an IBM name are named by function.

### `pmwin/` (IBM `ps\pmwin`)

| 2ine unit | IBM unit | APIs and services |
|---|---|---|
| `context.c` | unknown (`wininit1.c`/`wininit2.c` cover initialisation) | Anchor blocks, WinInitialize/WinTerminate, error info (WinGetLastError, WinGetErrorInfo, WinFreeErrorInfo, WinSetErrorInfo) |
| `winloop.c` | `winloop.c` | WinGetMsg, WinPeekMsg, WinDispatchMsg, WM_PAINT synthesis, event pumping |
| `messages.c` | unknown | Message queues, send and post, WinBroadcastMsg, WinQueryMsgPos, WinQueryQueueInfo |
| `thread_messages.c` | unknown | Cross-thread and foreign-process sends and posts |
| `host_events.c` | unknown | Host input and window events turned into queue messages |
| `present.c` | unknown | Presentation of finished window canvases |
| `winpos.c` | `winpos.c` | WinSetWindowPos, WinSetMultWindowPos, WinShowWindow, WinSetParent, WinQueryWindowPos/Rect, minimised and maximised positions, WinIsWindowShowing, WinFlashWindow |
| `windows.c` | unknown | WinCreateWindow, WinCreateStdWindow, WinDestroyWindow, WinIsWindow*, WinEnableWindow, window-destroy hooks |
| `classes.c` | unknown | Preregistered WC_* classes, WinRegisterClass |
| `defwndproc.c` | unknown | WinDefWindowProc |
| `frame_state.c` | unknown | Frame window: chrome, formatting, tracking, system commands, WinCreateFrameControls, WinCalcFrameRect |
| `window_data.c` | unknown | Window words, WinSetWindowBits, subclassing, WinWindowFromID, WinQueryClassInfo |
| `window_queries.c` | unknown | Enumeration, WinQueryWindow, WinQueryDesktopWindow, WinQueryObjectWindow, WinQueryWindowProcess |
| `window_text.c` | unknown | Window and dialog item text |
| `winpres.c` | `winpres.c` | Presentation parameters |
| `create_message.c` | unknown | WM_CREATE and owner-item delivery |
| `input.c` | `input.c` | Focus, activation, capture, WinFocusChange, WinGetCurrentTime |
| `keyxlate.c` | `keyxlate.c` | Key state, WinSetKeyboardStateTable, host keys to WM_CHAR |
| `accelerators.c` | unknown | Accelerator tables, WinTranslateAccel |
| `hooks.c` | unknown | WinSetHook, WinReleaseHook, input filter |
| `timers.c` | unknown | WinStartTimer, WinStopTimer |
| `pointers.c`, `pointer_image.c` | unknown | Pointers and cursor, WinQueryPointerPos |
| `paint.c` | unknown | WinBeginPaint/EndPaint, WinFillRect, WinDrawBorder |
| `winps.c` | `winps.c` (inferred correspondence) | Presentation spaces: the one handle table and lookup (lx_pm_font_space), default attributes, WinGetPS/GetScreenPS/ReleasePS, GpiCreatePS/DestroyPS backing, current position and path recording |
| `winclr.c` | `winclr.c` (inferred correspondence) | PS colours: colour indexes, logical colour table, RGB mode, WinRealizePalette |
| `invalidation.c` | unknown | Update regions, WinInvalidate*/Validate*, WinUpdateWindow |
| `visible.c` | unknown | Visible regions and update lock |
| `scroll.c` | unknown | WinScrollWindow |
| `draw.c` | unknown | WinInvertRect, WinDrawBitmap |
| `sysbitmap.c` | unknown | WinGetSysBitmap |
| `device_context.c` | unknown | Device contexts and display contexts, WinOpenWindowDC/QueryWindowDC |
| `winprint.c` | `winprint.c` | Printer device contexts and print escapes |
| `print_job.c` | unknown | PDF/PCL page output for printer contexts |
| `dialogs.c` | unknown | Modal loop, WinDlgBox, WinLoadDlg, WinProcessDlg, WinDefDlgProc, WinDismissDlg, dialog item shorts |
| `dialog_template.c` | unknown | Dialog template validation and creation |
| `message_box.c` | unknown | WinMessageBox, WinAlarm |
| `menus.c` | unknown | Menus |
| `clipboard.c` | unknown | Clipboard formats, ownership and host transfers |
| `atoms.c` | unknown | Atom tables |
| `codepages.c` | unknown (`strcp.c` likely covers code-page strings) | All PM code-page handling: WinCpTranslateChar/String, WinNextChar, WinSetCp/QueryCp, WinQueryCpType, the PS code page (GpiSetCp/QueryCp backing) |
| `strings.c` | unknown | WinCompareStrings, WinUpper, WinUpperChar |
| `resources.c` | unknown | WinLoadString, WinLoadMessage, WinLoad/DeleteLibrary |
| `system.c` | unknown | System colours and values |
| `semaphore_waits.c` | unknown | WinWaitEventSem, WinRequestMutexSem, WinWaitMuxWaitSem |
| `tracking.c` | unknown | WinTrackRect |
| `winshut.c` | `winshut.c` | WinCancelShutdown |
| `dive_targets.c`, `video_frames.c` | — (2ine) | The PM thread's side of DIVE blits and MDM video frames |

`dive_targets.c` and `video_frames.c` stay here rather than in DIVE and MDM:
PMMERGE's message loop pumps them, window destruction and anchor cleanup
release them, and they read PMMERGE's window state.

### `pmgre/` (IBM `ps\pmgre`, `ps\pmgre\386`)

| 2ine unit | IBM unit | Services |
|---|---|---|
| `32xform.c` | `32xform.c` | Transforms, page units, coordinate conversion, clipping limits, correlation |
| `32bitmap.c` | `32bitmap.c` | Bitmap objects, raster read/write |
| `32chstr.c` | `32chstr.c` | Character string engine and the PS font (lx_pm_font) |
| `raster.c` | unknown | Raster primitives: mixes, patterns, spans, lines |
| `regions.c` | unknown | Region engine |
| `dispatch.c` | unknown | Gre32Entry3, Gre32Entry5 |
| `graphics.h`, `gpi.h` | — | Engine interface and state types, used by PMGPI, PMWINX, PMCTLS, PMWP, DIVE and OPENGL |

### `ishapi/` (IBM `ps\ishapi`; the link to PMSHAPI is inferred)

| 2ine unit | IBM unit | APIs |
|---|---|---|
| `readini.c` | `readini.c` | Prf* profiles |
| `startapp.c` | unknown | WinStartApp |
| `switches.c` | unknown | Switch list |

### `controls/pmwctls/` (IBM `ps\controls\pmwctls`)

Which controls IBM placed in `pmwctls` rather than `pmwin` is unknown
(`bdedit2.c` is the only evidenced unit).

| 2ine unit | Control |
|---|---|
| `controls.c`, `controls.h` | Control framework: record, creation, painting, dispatch |
| `buttons.c` | WC_BUTTON |
| `entry.c` | WC_ENTRYFIELD |
| `combo.c` | WC_COMBOBOX |
| `listbox.c` | WC_LISTBOX |
| `static.c` | WC_STATIC |
| `scrollbar.c` | WC_SCROLLBAR |
| `mle.c` | WC_MLE |
| `spin.c` | WC_SPINBUTTON |

Warp's PMCTLS implements the MLE (PMMLE.1–3 forward to PMCTLS.31–33) and
exports REGISTERSPINBUTTON. In 2ine both are kinds of the control framework
above, and Warp's shell registers PMCTLS's public classes at startup, for
which 2ine has no process; so both stay in PMMERGE.

### `module.c`

The DLL entry: process display start-up for PM programs, the export table
and the default guest window and dialog procedures.
