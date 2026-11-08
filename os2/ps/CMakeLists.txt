# Derived from original 2ine; zlib-style license. See ../../LICENSE.txt.
# Presentation Manager (IBM w45ps\src\ps).
twoine_os2_dll(
    pmwin
    pmwin/help_compat.c
    pmwin/module.c
    pmwin/context.c
    pmwin/codepages.c
    pmwin/windows.c
    pmwin/frame_state.c
    pmwin/messages.c
    pmwin/paint.c
    pmwin/timers.c
    pmwin/graphics.c
    pmwin/raster.c
    pmwin/coordinates.c
    pmwin/device_context.c
    pmwin/print_job.c
    pmwin/resources.c
    pmwin/window_data.c
    pmwin/system.c
    pmwin/window_queries.c
    pmwin/presentation.c
    pmwin/create_message.c
    pmwin/dialogs.c
    pmwin/dialog_template.c
    pmwin/message_box.c
    pmwin/controls.c
    pmwin/buttons.c
    pmwin/bitmap.c
    pmwin/text.c
    pmwin/rectangles.c
    pmwin/entry.c
    pmwin/spin.c
    pmwin/scrollbar.c
    pmwin/combo.c
    pmwin/mle.c
    pmwin/standard_file.c
    pmwin/menus.c
    pmwin/regions.c
    pmwin/invalidation.c
    pmwin/accelerators.c
    pmwin/pointers.c
    pmwin/hooks.c
    pmwin/visible.c
    pmwin/dive.c
    pmwin/video_frames.c
    pmwin/thread_messages.c
    pmwin/input.c
    pmwin/window_text.c
    pmwin/clipboard.c
    pmwin/semaphore_waits.c
    pmwin/pointer_image.c
    pmwin/compat_services.c
    pmwin/standard_font.c
)
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
twoine_os2_dll(pmmerge pmmerge/module.c)
twoine_os2_dll(pmgre pmgre/module.c)
twoine_os2_dll(pmshapi pmshapi/module.c pmshapi/profiles.c pmshapi/switches.c pmshapi/startapp.c)
twoine_os2_dll(pmctls controls/pmctls/module.c)
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
target_link_libraries(pmwin PRIVATE twoine_formats nls)
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
    target_include_directories(pmwin PRIVATE ${ZLIB_INCLUDE_DIRS})
    target_link_libraries(pmwin PRIVATE "${_twoine_native_zlib}")
else()
    target_link_libraries(pmwin PRIVATE ZLIB::ZLIB)
endif()
target_link_libraries(pmgpi PRIVATE pmwin)
target_link_libraries(
    pmwinx
    PRIVATE pmshapi pmwin pmgpi pmdrag pmwp
)
target_link_libraries(pmshapi PRIVATE doscalls pmwin nls sesmgr)
target_link_libraries(pmdrag PRIVATE pmwin)
target_link_libraries(pmctls PRIVATE pmwin)
target_link_libraries(pmmerge PRIVATE pmwin)
