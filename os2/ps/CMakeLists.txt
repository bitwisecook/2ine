# Derived from original 2ine; zlib-style license. See ../../LICENSE.txt.
# Presentation Manager (IBM w45ps\src\ps).
twoine_os2_dll(
    pmgpi
    pmgpi/module.c
    pmgpi/fonts.c
    pmgpi/device.c
    pmgpi/state.c
    pmgpi/bitmap.c
    pmgpi/regions.c
    pmgpi/text.c
    pmgpi/shapes.c
    pmgpi/presentation_space.c
    pmgpi/transforms.c
    pmgpi/correlation.c
    pmgpi/color.c
    pmgpi/paths.c
    pmgpi/metafiles.c
    pmgpi/public_fonts.c
)
twoine_os2_dll(
    pmmerge
    pmmerge/module.c
    pmmerge/pmgre/dispatch.c
    pmmerge/ishapi/readini.c
    pmmerge/ishapi/switches.c
    pmmerge/ishapi/startapp.c
    pmmerge/pmwin/strings.c
    pmmerge/pmwin/scroll.c
    pmmerge/pmwin/context.c
    pmmerge/pmwin/codepages.c
    pmmerge/pmwin/windows.c
    pmmerge/pmwin/classes.c
    pmmerge/pmwin/defwndproc.c
    pmmerge/pmwin/frame_state.c
    pmmerge/pmwin/messages.c
    pmmerge/pmwin/winloop.c
    pmmerge/pmwin/host_events.c
    pmmerge/pmwin/present.c
    pmmerge/pmwin/paint.c
    pmmerge/pmwin/timers.c
    pmmerge/pmwin/winps.c
    pmmerge/pmwin/winclr.c
    pmmerge/pmgre/raster.c
    pmmerge/pmgre/rasterize.c
    pmmerge/pmgre/32xform.c
    pmmerge/pmwin/device_context.c
    pmmerge/pmwin/winprint.c
    pmmerge/pmwin/print_job.c
    pmmerge/pmwin/resources.c
    pmmerge/pmwin/window_data.c
    pmmerge/pmwin/system.c
    pmmerge/pmwin/window_queries.c
    pmmerge/pmwin/winpres.c
    pmmerge/pmwin/create_message.c
    pmmerge/pmwin/dialogs.c
    pmmerge/pmwin/dialog_template.c
    pmmerge/pmwin/message_box.c
    pmmerge/controls/pmwctls/controls.c
    pmmerge/controls/pmwctls/listbox.c
    pmmerge/controls/pmwctls/static.c
    pmmerge/controls/pmwctls/buttons.c
    pmmerge/pmgre/32bitmap.c
    pmmerge/pmwin/sysbitmap.c
    pmmerge/pmwin/draw.c
    pmmerge/pmgre/32chstr.c
    pmmerge/pmwin/rectangles.c
    pmmerge/controls/pmwctls/entry.c
    pmmerge/controls/pmwctls/spin.c
    pmmerge/controls/pmwctls/scrollbar.c
    pmmerge/controls/pmwctls/combo.c
    pmmerge/controls/pmwctls/mle.c
    pmmerge/pmwin/menus.c
    pmmerge/pmgre/regions.c
    pmmerge/pmwin/invalidation.c
    pmmerge/pmwin/accelerators.c
    pmmerge/pmwin/pointers.c
    pmmerge/pmwin/hooks.c
    pmmerge/pmwin/visible.c
    pmmerge/pmwin/dive_targets.c
    pmmerge/pmwin/video_frames.c
    pmmerge/pmwin/thread_messages.c
    pmmerge/pmwin/input.c
    pmmerge/pmwin/keyxlate.c
    pmmerge/pmwin/window_text.c
    pmmerge/pmwin/clipboard.c
    pmmerge/pmwin/atoms.c
    pmmerge/pmwin/semaphore_waits.c
    pmmerge/pmwin/pointer_image.c
    pmmerge/pmwin/winshut.c
    pmmerge/pmwin/winpos.c
    pmmerge/pmwin/tracking.c
)
twoine_os2_dll(pmwin pmwin/module.c)
twoine_os2_dll(pmgre pmgre/module.c)
twoine_os2_dll(pmshapi pmshapi/module.c)
twoine_os2_dll(
    pmctls
    controls/pmctls/module.c
    controls/pmctls/dialogs/filedlgm.c
    controls/pmctls/dialogs/fontdlg.c
    controls/pmctls/drag/drgutil.c
    controls/pmctls/drag/drag_loop.c
)
twoine_os2_dll(pmdrag controls/drag/module.c)
twoine_os2_dll(helpmgr helpmgr/module.c)
twoine_os2_dll(
    pmwinx
    awe/module.c
    awe/kernel/critical_sections.c
    awe/kernel/environment.c
    awe/kernel/files/file_operations.c
    awe/kernel/system.c
    awe/kernel/events_mutexes.c
    awe/kernel/files/directories.c
    awe/kernel/files/file_handles.c
    awe/kernel/files/find.c
    awe/kernel/files/legacy_files.c
    awe/kernel/interlocked.c
    awe/kernel/locale.c
    awe/kernel/memory.c
    awe/kernel/modules.c
    awe/kernel/priority.c
    awe/kernel/processes.c
    awe/kernel/profiles.c
    awe/kernel/resources.c
    awe/kernel/semaphores.c
    awe/kernel/threads.c
    awe/kernel/time.c
    awe/kernel/time_zones.c
    awe/kernel/tls.c
    awe/kernel/unicode.c
    awe/kernel/validation.c
    awe/user/atoms.c
    awe/user/clipboard.c
    awe/user/controls/native_controls.c
    awe/user/controls/scroll_info.c
    awe/user/controls/scrollbar.c
    awe/user/dialogs/dialog_helpers.c
    awe/user/dialogs/dialogs.c
    awe/user/dialogs/message_box.c
    awe/user/help.c
    awe/user/input/accelerators.c
    awe/user/input/caret.c
    awe/user/input/cursors.c
    awe/user/input/icons.c
    awe/user/input/keyboard.c
    awe/user/input/mouse.c
    awe/user/menus.c
    awe/user/messages/hooks.c
    awe/user/messages/queue.c
    awe/user/messages/timers.c
    awe/user/painting/redraw.c
    awe/user/painting/scrolling.c
    awe/user/rectangles.c
    awe/user/strings/format.c
    awe/user/strings/strings.c
    awe/user/system_metrics.c
    awe/user/windows/activation.c
    awe/user/windows/adjust_window.c
    awe/user/windows/classes.c
    awe/user/windows/defer_position.c
    awe/user/windows/enumeration.c
    awe/user/windows/geometry.c
    awe/user/windows/mdi.c
    awe/user/windows/properties.c
    awe/user/windows/windows.c
    awe/gdi/bitmaps.c
    awe/gdi/brushes.c
    awe/gdi/dc.c
    awe/gdi/dc_attributes.c
    awe/gdi/dc_state.c
    awe/gdi/dib.c
    awe/gdi/font_metrics.c
    awe/gdi/fonts.c
    awe/gdi/mapping_modes.c
    awe/gdi/metafiles/wmf.c
    awe/gdi/palettes.c
    awe/gdi/paths.c
    awe/gdi/pens.c
    awe/gdi/pm_interop.c
    awe/gdi/printing.c
    awe/gdi/regions.c
    awe/gdi/text.c
    awe/comdlg/color_dialog.c
    awe/comdlg/file_dialogs.c
    awe/shell/drag_drop.c
    awe/dde/conversations.c
)
target_link_libraries(pmmerge PRIVATE twoine_formats doscalls)
find_package(ZLIB REQUIRED)
if(TWOINE_NATIVE_LOADER)
    # This target uses -m32 while CMake's configure process runs on the host ABI.
    # Ask the compiler for its target library instead of FindZLIB's host result.
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" -m32 -print-file-name=libz.so
        OUTPUT_VARIABLE _twoine_native_zlib
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    if(NOT IS_ABSOLUTE "${_twoine_native_zlib}" OR NOT EXISTS "${_twoine_native_zlib}")
        message(FATAL_ERROR "Install the i386 zlib development library for the native worker")
    endif()
    target_include_directories(pmmerge PRIVATE ${ZLIB_INCLUDE_DIRS})
    target_link_libraries(pmmerge PRIVATE "${_twoine_native_zlib}")
else()
    target_link_libraries(pmmerge PRIVATE ZLIB::ZLIB)
endif()
target_link_libraries(pmgpi PRIVATE pmmerge)
target_link_libraries(pmwinx PRIVATE pmmerge pmgpi pmctls pmwp)
target_link_libraries(pmctls PRIVATE pmmerge)
