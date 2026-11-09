# Public domain. No rights reserved. Wholly AI-authored.
# Presentation Manager: PMMERGE, PMGPI, PMSPL, HELPMGR, profiles and settings.
add_executable(twoine_printer_catalogue_tests test_printer_catalogue.c)
target_include_directories(twoine_printer_catalogue_tests PRIVATE "${PROJECT_SOURCE_DIR}")
add_test(NAME printer_catalogue COMMAND twoine_printer_catalogue_tests)
set_tests_properties(printer_catalogue PROPERTIES TIMEOUT 20)
if(TARGET pmmerge)
    add_executable(twoine_pm_tests test_pm.c)
    add_executable(twoine_clipboard_pm_tests test_clipboard.c)
    twoine_application_target(twoine_clipboard_pm_tests)
    target_link_libraries(twoine_clipboard_pm_tests PRIVATE pmmerge 2ine SDL3::SDL3)
    add_test(NAME clipboard_pm COMMAND twoine_clipboard_pm_tests)
    set_tests_properties(
        clipboard_pm
        PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
    )
    add_executable(twoine_clipboard_memory_hook_tests test_clipboard_memory_hook.c)
    twoine_application_target(twoine_clipboard_memory_hook_tests)
    target_link_libraries(twoine_clipboard_memory_hook_tests PRIVATE 2ine)
    add_test(
        NAME clipboard_memory_hook
        COMMAND twoine_clipboard_memory_hook_tests $<TARGET_FILE:doscalls>
    )
    set_tests_properties(clipboard_memory_hook PROPERTIES TIMEOUT 10)
    twoine_application_target(twoine_pm_tests)
    target_link_libraries(twoine_pm_tests PRIVATE pmgpi pmmerge 2ine)
    add_test(NAME presentation_manager COMMAND twoine_pm_tests)
    set_tests_properties(
        presentation_manager
        PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
    )
    add_executable(twoine_pm_raster_tests test_pm_raster.c)
    twoine_application_target(twoine_pm_raster_tests)
    target_link_libraries(twoine_pm_raster_tests PRIVATE pmwinx pmgpi pmmerge 2ine)
    add_test(NAME pm_raster COMMAND twoine_pm_raster_tests)
    set_tests_properties(
        pm_raster
        PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
    )
    add_executable(twoine_help_font_tests test_help_fonts.c)
    twoine_application_target(twoine_help_font_tests)
    target_link_libraries(twoine_help_font_tests PRIVATE pmgpi pmmerge 2ine)
    add_test(NAME help_font_transitions COMMAND twoine_help_font_tests)
    set_tests_properties(
        help_font_transitions
        PROPERTIES
            TIMEOUT 20
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software;TWOINE_DISPLAY_DPI=96"
    )
    add_executable(twoine_frame_format_tests test_frame_format.c)
    twoine_application_target(twoine_frame_format_tests)
    target_link_libraries(twoine_frame_format_tests PRIVATE pmmerge 2ine)
    add_test(NAME frame_format_protocol COMMAND twoine_frame_format_tests)
    set_tests_properties(
        frame_format_protocol
        PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
    )
    add_executable(twoine_display_dpi_tests test_display_dpi.c)
    twoine_application_target(twoine_display_dpi_tests)
    target_link_libraries(twoine_display_dpi_tests PRIVATE pmgpi pmmerge 2ine)
    foreach(dpi 96 120 144 192)
        add_test(NAME display_dpi_${dpi} COMMAND twoine_display_dpi_tests ${dpi})
        set_tests_properties(
            display_dpi_${dpi}
            PROPERTIES
                TIMEOUT 20
                ENVIRONMENT
                    "TWOINE_DISPLAY_DPI=${dpi};SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
        )
    endforeach()
    foreach(depth 4 8 16)
        add_test(NAME display_depth_${depth} COMMAND twoine_display_dpi_tests 96)
        set_tests_properties(
            display_depth_${depth}
            PROPERTIES
                TIMEOUT 20
                ENVIRONMENT
                    "TWOINE_DISPLAY_DPI=96;TWOINE_DISPLAY_DEPTH=${depth};SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
        )
    endforeach()
    add_executable(twoine_menu_chrome_tests test_menu_chrome.c)
    twoine_application_target(twoine_menu_chrome_tests)
    target_link_libraries(twoine_menu_chrome_tests PRIVATE pmmerge 2ine)
    add_test(NAME pm_menu_chrome COMMAND twoine_menu_chrome_tests)
    set_tests_properties(
        pm_menu_chrome
        PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy"
    )
    add_executable(twoine_printing_tests test_printing.c)
    twoine_application_target(twoine_printing_tests)
    target_link_libraries(twoine_printing_tests PRIVATE pmgpi pmmerge pmspl 2ine)
    add_test(
        NAME printer_output
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/ps/printing.py"
            --build "${PROJECT_BINARY_DIR}" --backend
            "$<IF:$<BOOL:${TWOINE_PORTABLE_LOADER}>,unicorn,native>"
    )
    set_tests_properties(
        printer_output
        PROPERTIES TIMEOUT 60 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
    )
    add_executable(twoine_dialog_tests test_dialogs.c)
    twoine_application_target(twoine_dialog_tests)
    target_link_libraries(twoine_dialog_tests PRIVATE pmmerge 2ine)
    add_test(NAME pm_dialogs COMMAND twoine_dialog_tests)
    set_tests_properties(
        pm_dialogs
        PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
    )
endif()

if(TARGET pmwinx)
    add_executable(twoine_settings_tests test_settings.c)
    twoine_application_target(twoine_settings_tests)
    target_link_libraries(
        twoine_settings_tests
        PRIVATE pmmerge pmwinx registry pmwp helpmgr 2ine
    )
    add_test(
        NAME application_settings
        COMMAND
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/settings.py"
            $<TARGET_FILE:twoine_settings_tests>
    )
    set_tests_properties(application_settings PROPERTIES TIMEOUT 40)
endif()

if(TARGET pmmerge AND UNIX)
    add_executable(twoine_profile_worker profile_worker.c)
    twoine_application_target(twoine_profile_worker)
    target_link_libraries(twoine_profile_worker PRIVATE pmmerge 2ine)
    add_test(
        NAME profile_concurrency
        COMMAND
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/profile_concurrency.py"
            $<TARGET_FILE:twoine_profile_worker>
    )
    set_tests_properties(profile_concurrency PROPERTIES TIMEOUT 45)
endif()

if(TARGET dive)
    add_executable(twoine_visibility_tests test_visibility.c)
    twoine_application_target(twoine_visibility_tests)
    target_link_libraries(twoine_visibility_tests PRIVATE pmgpi pmmerge 2ine)
    add_test(NAME pm_visible_regions COMMAND twoine_visibility_tests)
    set_tests_properties(
        pm_visible_regions
        PROPERTIES
            TIMEOUT 20
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
    )
endif()

if(TARGET pmmerge)
    add_executable(twoine_pm_thread_tests test_pm_threads.c)
    twoine_application_target(twoine_pm_thread_tests)
    target_link_libraries(twoine_pm_thread_tests PRIVATE pmmerge 2ine)
    add_test(NAME pm_thread_posts COMMAND twoine_pm_thread_tests)
    set_tests_properties(
        pm_thread_posts
        PROPERTIES
            TIMEOUT 20
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
    )
endif()

if(TARGET pmmerge AND TARGET pmgpi)
    add_executable(twoine_help_api_tests test_help_apis.c)
    twoine_application_target(twoine_help_api_tests)
    target_link_libraries(twoine_help_api_tests PRIVATE pmmerge pmgpi 2ine)
    add_test(NAME help_prerequisite_apis COMMAND twoine_help_api_tests)
    set_tests_properties(
        help_prerequisite_apis
        PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
    )
endif()

if(TARGET pmgpi AND TARGET pmmerge)
    add_executable(twoine_bitmap_bits_tests test_bitmap_bits.c)
    twoine_application_target(twoine_bitmap_bits_tests)
    target_link_libraries(twoine_bitmap_bits_tests PRIVATE pmgpi pmmerge 2ine)
    add_test(NAME bitmap_bits COMMAND twoine_bitmap_bits_tests)
    set_tests_properties(
        bitmap_bits
        PROPERTIES
            TIMEOUT 15
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
    )
endif()
