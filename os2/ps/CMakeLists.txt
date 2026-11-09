# Derived from original 2ine; zlib-style license. See ../../LICENSE.txt.
# Presentation Manager (IBM w45ps\src\ps).
twoine_os2_dll(
    pmgpi
    pmgpi/module.c
    pmgpi/help.c
    pmgpi/fonts.c
    pmgpi/device.c
    pmgpi/state.c
    pmgpi/bitmap.c
    pmgpi/regions.c
    pmgpi/text.c
    pmgpi/shapes.c
    pmgpi/extensions.c
    pmgpi/paths.c
    pmgpi/metafiles.c
    pmgpi/public_fonts.c
)
twoine_os2_dll(
    pmmerge
    pmmerge/module.c
    pmmerge/pmgre/dispatch.c
    pmmerge/ishapi/profiles.c
    pmmerge/ishapi/switches.c
    pmmerge/ishapi/startapp.c
    pmmerge/pmwin/cptype.c
    pmmerge/pmwin/strings.c
    pmmerge/pmwin/scroll.c
    pmmerge/pmwin/context.c
    pmmerge/pmwin/codepages.c
    pmmerge/pmwin/windows.c
    pmmerge/pmwin/frame_state.c
    pmmerge/pmwin/messages.c
    pmmerge/pmwin/paint.c
    pmmerge/pmwin/timers.c
    pmmerge/pmwin/graphics.c
    pmmerge/pmwin/raster.c
    pmmerge/pmwin/coordinates.c
    pmmerge/pmwin/device_context.c
    pmmerge/pmwin/print_job.c
    pmmerge/pmwin/resources.c
    pmmerge/pmwin/window_data.c
    pmmerge/pmwin/system.c
    pmmerge/pmwin/window_queries.c
    pmmerge/pmwin/presentation.c
    pmmerge/pmwin/create_message.c
    pmmerge/pmwin/dialogs.c
    pmmerge/pmwin/dialog_template.c
    pmmerge/pmwin/message_box.c
    pmmerge/pmwin/controls.c
    pmmerge/pmwin/listbox.c
    pmmerge/pmwin/static.c
    pmmerge/pmwin/buttons.c
    pmmerge/pmwin/bitmap.c
    pmmerge/pmwin/text.c
    pmmerge/pmwin/rectangles.c
    pmmerge/pmwin/entry.c
    pmmerge/pmwin/spin.c
    pmmerge/pmwin/scrollbar.c
    pmmerge/pmwin/combo.c
    pmmerge/pmwin/mle.c
    pmmerge/pmwin/menus.c
    pmmerge/pmwin/regions.c
    pmmerge/pmwin/invalidation.c
    pmmerge/pmwin/accelerators.c
    pmmerge/pmwin/pointers.c
    pmmerge/pmwin/hooks.c
    pmmerge/pmwin/visible.c
    pmmerge/pmwin/dive.c
    pmmerge/pmwin/video_frames.c
    pmmerge/pmwin/thread_messages.c
    pmmerge/pmwin/input.c
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
    awe/alerts.c
    awe/help.c
    awe/time.c
    awe/timers.c
    awe/locale.c
    awe/unicode.c
    awe/context.c
    awe/dde.c
    awe/registry.c
    awe/modules.c
    awe/resources.c
    awe/accelerators.c
    awe/profiles.c
    awe/atoms.c
    awe/classes.c
    awe/windows.c
    awe/enumeration.c
    awe/dialogs.c
    awe/file_dialogs.c
    awe/printing.c
    awe/native_controls.c
    awe/mdi.c
    awe/properties.c
    awe/caret.c
    awe/clipboard.c
    awe/hooks.c
    awe/input.c
    awe/geometry.c
    awe/regions.c
    awe/rectangles.c
    awe/redraw.c
    awe/messages.c
    awe/memory.c
    awe/validation.c
    awe/menus.c
    awe/bitmap.c
    awe/metafile.c
    awe/brushes.c
    awe/pens.c
    awe/palettes.c
    awe/metrics.c
    awe/dib.c
    awe/drawing.c
    awe/dc_state.c
    awe/fonts.c
    awe/font_metrics.c
    awe/scroll.c
    awe/scroll_control.c
    awe/strings.c
    awe/text.c
    awe/files.c
    awe/file_handles.c
    awe/find.c
    awe/paths.c
    awe/format.c
    awe/pointers.c
    awe/icons.c
    awe/focus.c
    awe/critical.c
    awe/tls.c
    awe/threads.c
    awe/priority.c
    awe/atomic.c
    awe/adjust_window.c
    awe/semaphores.c
    awe/batch_graphics_state.c
    awe/batch_graphics_paths.c
    awe/batch_graphics_scroll.c
    awe/batch_graphics_interop.c
    awe/batch_services_basic.c
    awe/batch_services_common_dialogs.c
    awe/batch_services_drag.c
    awe/batch_services_paths.c
    awe/batch_services_presentation.c
    awe/batch_services_sync.c
    awe/batch_services_windows.c
    awe/process.c
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
