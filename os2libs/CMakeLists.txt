# Derived from original 2ine; zlib-style license. See ../LICENSE.txt.
foreach(module doscalls msg nls quecalls viocalls kbdcalls sesmgr som pmmerge pmwin pmshapi pmwp pmgpi tcpip32 pmwinx pmctls helpmgr mdm mmio pmspl pmdrag dive pmgre so32dll tcp32dll)
    if(module STREQUAL "doscalls")
        set(sources doscalls/module.c doscalls/legacy.c doscalls/files.c doscalls/fileinfo.c doscalls/eas.c doscalls/directories.c doscalls/paths.c doscalls/resources.c doscalls/module_resources.c doscalls/exceptions.c doscalls/memory.c doscalls/suballocation.c doscalls/threads.c doscalls/process.c doscalls/semaphores.c doscalls/modules.c doscalls/system.c doscalls/audio.c doscalls/mux_wait.c)
    elseif(module STREQUAL "mdm")
        set(sources mdm.c mdm_video.c video_decode.c midi_synth.c midi_config.c midi_mt32.c)
    elseif(module STREQUAL "pmwin")
        set(sources pmwin/help_compat.c pmwin/module.c pmwin/context.c pmwin/codepages.c pmwin/windows.c pmwin/frame_state.c pmwin/messages.c pmwin/paint.c pmwin/timers.c pmwin/graphics.c pmwin/raster.c pmwin/coordinates.c pmwin/device_context.c pmwin/print_job.c pmwin/resources.c pmwin/window_data.c pmwin/system.c pmwin/window_queries.c pmwin/presentation.c pmwin/create_message.c pmwin/dialogs.c pmwin/dialog_template.c pmwin/message_box.c pmwin/controls.c pmwin/buttons.c pmwin/bitmap.c pmwin/text.c pmwin/rectangles.c pmwin/entry.c pmwin/spin.c pmwin/scrollbar.c pmwin/combo.c pmwin/mle.c pmwin/standard_file.c pmwin/menus.c pmwin/regions.c pmwin/invalidation.c pmwin/accelerators.c pmwin/pointers.c pmwin/hooks.c pmwin/visible.c pmwin/dive.c pmwin/video_frames.c pmwin/thread_messages.c pmwin/input.c pmwin/window_text.c pmwin/clipboard.c pmwin/semaphore_waits.c pmwin/pointer_image.c ../ball/icon_bitmap.c)
    elseif(module STREQUAL "pmgpi")
        set(sources pmgpi.c pmgpi/help.c pmgpi/fonts.c pmgpi/device.c pmgpi/state.c pmgpi/bitmap.c pmgpi/regions.c pmgpi/text.c pmgpi/shapes.c)
    elseif(module STREQUAL "pmctls")
        set(sources pmctls/module.c)
    elseif(module STREQUAL "pmwinx")
        set(sources
            pmwinx/module.c
            pmwinx/alerts.c
            pmwinx/help.c
            pmwinx/time.c
            pmwinx/timers.c
            pmwinx/locale.c
            pmwinx/context.c
            pmwinx/dde.c
            pmwinx/registry.c
            pmwinx/modules.c
            pmwinx/resources.c
            pmwinx/profiles.c
            pmwinx/atoms.c
            pmwinx/classes.c
            pmwinx/windows.c
            pmwinx/enumeration.c
            pmwinx/dialogs.c
            pmwinx/file_dialogs.c
            pmwinx/printing.c
            pmwinx/native_controls.c
            pmwinx/mdi.c
            pmwinx/properties.c
            pmwinx/caret.c
            pmwinx/clipboard.c
            pmwinx/hooks.c
            pmwinx/input.c
            pmwinx/geometry.c
            pmwinx/regions.c
            pmwinx/rectangles.c
            pmwinx/redraw.c
            pmwinx/messages.c
            pmwinx/memory.c
            pmwinx/validation.c
            pmwinx/menus.c
            pmwinx/bitmap.c
            pmwinx/brushes.c
            pmwinx/pens.c
            pmwinx/palettes.c
            pmwinx/metrics.c
            pmwinx/dib.c
            pmwinx/drawing.c
            pmwinx/dc_state.c
            pmwinx/fonts.c
            pmwinx/font_metrics.c
            pmwinx/scroll.c
            pmwinx/scroll_control.c
            pmwinx/strings.c
            pmwinx/text.c
            pmwinx/files.c
            pmwinx/file_handles.c
            pmwinx/find.c
            pmwinx/paths.c
            pmwinx/format.c
            pmwinx/pointers.c
            pmwinx/icons.c
            pmwinx/focus.c
            pmwinx/critical.c
            pmwinx/threads.c
        )
    elseif(module STREQUAL "pmshapi")
        set(sources pmshapi.c pmshapi/profiles.c pmshapi/switches.c)
    else()
        set(sources "${module}.c")
    endif()
    add_library(${module} SHARED ${sources})
    twoine_application_target(${module})
    target_link_libraries(${module} PRIVATE 2ine)
    set_target_properties(${module} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    if(APPLE)
        set_target_properties(${module} PROPERTIES SUFFIX ".so")
    endif()
endforeach()
if(TWOINE_NATIVE_LOADER)
    twoine_hal_add_native_abi(doscalls doscalls)
    twoine_hal_add_native_abi(msg msg)
endif()
twoine_add_terminal(viocalls)
target_link_libraries(kbdcalls PRIVATE viocalls)

target_link_libraries(quecalls PRIVATE doscalls)
target_link_libraries(sesmgr PRIVATE doscalls quecalls pmwin)
find_package(ZLIB REQUIRED)
target_link_libraries(pmwin PRIVATE ZLIB::ZLIB)
target_link_libraries(pmgpi PRIVATE pmwin)
target_link_libraries(pmwinx PRIVATE pmshapi pmwin pmgpi)
target_link_libraries(pmwp PRIVATE pmshapi)
target_link_libraries(pmshapi PRIVATE pmwin nls)
target_link_libraries(pmspl PRIVATE pmshapi)

target_link_libraries(pmctls PRIVATE pmwin)
target_link_libraries(pmwin PRIVATE nls)
target_link_libraries(pmmerge PRIVATE pmwin)

if(TWOINE_OPENGL)
    add_library(opengl SHARED opengl/module.c opengl/pgl.c opengl/memory.c opengl/custom.c opengl/arrays.c)
    twoine_application_target(opengl)
    target_link_libraries(opengl PRIVATE pmwin 2ine)
    twoine_add_opengl(opengl)
    set_target_properties(opengl PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    if(APPLE)
        set_target_properties(opengl PROPERTIES SUFFIX ".so")
    endif()
endif()


target_link_libraries(dive PRIVATE pmwin)

target_link_libraries(mdm PRIVATE mmio pmwin m twoine_midi_config twoine_mt32_config twoine_video_config)

if(TWOINE_MIDI_EXTENDED AND TWOINE_MIDI_SYNTH)
    target_sources(mdm PRIVATE midi_banks.cpp)
    target_compile_features(mdm PRIVATE cxx_std_17)
    set_target_properties(mdm PROPERTIES CXX_VISIBILITY_PRESET hidden)
endif()

if(TWOINE_VIDEO)
    add_dependencies(mdm twoine_ffmpeg_metadata)
endif()
