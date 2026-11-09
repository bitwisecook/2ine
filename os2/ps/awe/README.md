<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# PMWINX.DLL

Open32: the Win32 API over PM and the base OS.

- IBM source: `ps\awe\` (w*.cpp windows and controls, x*.cpp API layers, o*.cpp GDI objects; about 60 files).
- Evidence: `@BSS` labels in `os2/dll/pmwinx.sym`; SYM directory `ps/awe`.
- In Warp 4.52: Real code; its Reg* exports forward to REGISTRY.DLL.
- Exports: `pmwinx.def`.

## Units

The sub-directories group the units by the Win32 DLL whose API they
provide. IBM's own unit names are known from `@BSS` labels (listed above);
the correspondences in the table are inferred from those names alone and do
not claim that the code matches IBM's units. `os2/include/os2win.h` declares
the exports in the same groups.

| Directory | Units | Likely IBM units |
|---|---|---|
| (top) | `module.c` (DLL entry, WinInitializeOpen32, WinCallWinMain, WinXProcessMsg, the lazy PM anchor), `internal.h`, `bindings*.h` (guest ABI bridges: `bindings_gdi.h`, `bindings_services.h`, `bindings_process.h`, `bindings_unicode.h`) | `open.cpp`, `wentry.cpp` |
| `kernel/` (KERNEL32) | `system.c` (last error, versions, ticks, command line, Beep, debug output), `memory.c`, `validation.c`, `interlocked.c`, `critical_sections.c`, `tls.c`, `threads.c`, `processes.c`, `priority.c`, `semaphores.c`, `events_mutexes.c`, `environment.c`, `time.c`, `time_zones.c`, `locale.c`, `unicode.c`, `modules.c`, `resources.c`, `profiles.c` | `xprocess.cpp`, `wsystime.cpp`, `werror.cpp`, `wuls.cpp`, `unicode.c`, `dbcs.c`, `winidata.cpp` |
| `kernel/files/` | `file_handles.c` (CreateFile handles, locks, times), `legacy_files.c` (OpenFile, `_l*`), `file_operations.c` (copy, move, delete, attributes), `directories.c` (directories, drives, temporary files, SearchPath), `find.c` | `xfile.cpp` |
| `user/windows/` (USER32) | `windows.c`, `classes.c`, `geometry.c`, `adjust_window.c`, `enumeration.c` (window queries), `activation.c`, `defer_position.c`, `properties.c`, `mdi.c` | `xwindow.cpp`, `wbase.cpp`, `wclass.cpp`, `wframe.cpp`, `wtitle.cpp`, `xuser.cpp`, `dwp.cpp`, `wdefclnt.cpp`, `wmdiclnt.cpp` |
| `user/messages/` | `queue.c`, `timers.c`, `hooks.c` | `wxmsg.cpp`, `whooks.cpp` |
| `user/input/` | `keyboard.c`, `mouse.c`, `cursors.c`, `icons.c`, `caret.c`, `accelerators.c` | `wkey.cpp` |
| `user/painting/` | `redraw.c` (invalidation, LockWindowUpdate), `scrolling.c` (ScrollDC, ScrollWindow) | unknown |
| `user/controls/` | `native_controls.c` (Windows control classes over PM controls), `scrollbar.c` (SCROLLBAR class), `scroll_info.c` (scroll positions and ranges) | `wbutton.cpp`, `wcombo.cpp`, `wedit.cpp`, `wlistbox.cpp`, `wstatic.cpp`, `wscroll.cpp`, `wuserctl.cpp` |
| `user/dialogs/` | `dialogs.c` (dialog boxes, template conversion), `dialog_helpers.c`, `message_box.c` | `wdialog.cpp`, `xdialog.cpp` |
| `user/strings/` | `strings.c`, `format.c` (wsprintf), `case_tables.h` | `wstring.cpp`, `wdbcs.cpp` |
| `user/` | `menus.c`, `clipboard.c`, `atoms.c`, `rectangles.c`, `system_metrics.c`, `help.c` | `wmenubar.cpp`, `clipbrd.cpp` |
| `gdi/` (GDI32) | `dc.c` (DC objects, GetDC, CreateDC), `dc_attributes.c`, `dc_state.c` (SaveDC/RestoreDC), `mapping_modes.c` (and the world transform), `objects.c`, `device_caps.c`, `bitmaps.c`, `dib.c`, `brushes.c`, `fills.c`, `pens.c`, `lines_shapes.c`, `paths.c`, `regions.c`, `palettes.c`, `fonts.c`, `font_metrics.c`, `text.c`, `printing.c`, `pm_interop.c` (PM/Open32 handle and coordinate translation) | `xgdi1.cpp`, `xgdi2.cpp`, `obase.cpp`, `obitmap.cpp`, `xdibmap.cpp`, `obrush.cpp`, `oextpen.cpp`, `oregion.cpp`, `opalette.cpp`, `ofont.cpp`, `oprinter.cpp`, `sharedps.cpp` |
| `gdi/metafiles/` | `wmf.c`, `emf.c`, `recorder.c`, `conversion.c`, `metafile.h` (recording hooks), `metafile_objects.h` | `ometafil.cpp`, `owinmeta.cpp`, `oenhmeta.cpp` |
| `comdlg/` (COMDLG32) | `file_dialogs.c`, `font_dialog.c`, `color_dialog.c`, `print_dialog.c`, `common_dialogs.c` (hook and template helpers) | `wcommdlg.cpp` |
| `shell/` (SHELL32) | `drag_drop.c` (DragAcceptFiles, DragQueryFile), `execute.c` (ShellExecute) | `shell.cpp` |
| `dde/` (DDEML) | `instance.c`, `strings.c`, `data.c`, `callbacks.c`, `conversations.c`, `remote.c` (other processes of the ball), `dde.h` | `xdde.cpp` |

The Reg* exports forward to REGISTRY.DLL (`os2/registry/reg32`). Drawing
goes through PMMERGE's graphics engine (`os2/include/private/pmgrep.h`):
polygon fills, wide pens and polygon regions use its scan converter, lines
its line walk, and PatBlt, StretchDIBits and BitBlt its raster operations.
Some units are also built as object libraries for tests that replace the
services they call (`os2/ps/CMakeLists.txt`).
