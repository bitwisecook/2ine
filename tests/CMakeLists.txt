# Public domain. No rights reserved. Wholly AI-authored.
add_test(NAME source_boundaries COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/architecture/check_boundaries.py")
if(UNIX)
    add_executable(twoine_desktop_group_tests hal/test_desktop_group.c
        ../hal/common/desktop_group.c ../hal/posix/desktop_lease.c)
    target_include_directories(twoine_desktop_group_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_compile_features(twoine_desktop_group_tests PRIVATE c_std_99)
    add_test(NAME desktop_group COMMAND twoine_desktop_group_tests)
    set_tests_properties(desktop_group PROPERTIES TIMEOUT 15)
    add_executable(twoine_desktop_cycle_tests hal/test_desktop_cycle.c
        ../hal/common/desktop_cycle.c ../hal/posix/desktop_cycle.c ../hal/posix/desktop_lease.c)
    target_include_directories(twoine_desktop_cycle_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_compile_features(twoine_desktop_cycle_tests PRIVATE c_std_99)
    add_test(NAME desktop_cycle COMMAND twoine_desktop_cycle_tests)
    set_tests_properties(desktop_cycle PROPERTIES TIMEOUT 15)
endif()
if(TARGET opengl)
    add_executable(twoine_opengl_tests hal/test_opengl.c)
    twoine_application_target(twoine_opengl_tests)
    target_link_libraries(twoine_opengl_tests PRIVATE opengl)
    add_test(NAME opengl_mesa COMMAND twoine_opengl_tests)
    set_tests_properties(opengl_mesa PROPERTIES TIMEOUT 60)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_tests_properties(opengl_mesa PROPERTIES ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        add_executable(twoine_gl_policy_tests hal/test_gl_policy.c ../hal/macos/gl_policy.c)
        twoine_application_target(twoine_gl_policy_tests)
        target_compile_definitions(twoine_gl_policy_tests PRIVATE TWOINE_KOSMICKRISP_ICD="/twoine-test/kosmickrisp.json")
        add_test(NAME opengl_macos_policy COMMAND twoine_gl_policy_tests)
    endif()
    add_executable(twoine_opengl_pm_tests os2libs/test_opengl.c)
    twoine_application_target(twoine_opengl_pm_tests)
    target_link_libraries(twoine_opengl_pm_tests PRIVATE opengl pmwin pmgpi 2ine)
    add_test(NAME opengl_pm COMMAND twoine_opengl_pm_tests)
    set_tests_properties(opengl_pm PROPERTIES TIMEOUT 60 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_property(TEST opengl_pm APPEND PROPERTY ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
    endif()
endif()
if(TARGET 2ine)
    add_executable(twoine_clipboard_hal_tests hal/test_clipboard.c)
    twoine_application_target(twoine_clipboard_hal_tests)
    target_link_libraries(twoine_clipboard_hal_tests PRIVATE 2ine SDL3::SDL3)
    add_test(NAME clipboard_hal COMMAND twoine_clipboard_hal_tests)
    set_tests_properties(clipboard_hal PROPERTIES TIMEOUT 15 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
endif()
if(TARGET twoine_runtime)
    add_executable(twoine_image_fs_tests hal/test_image_fs.c)
    target_link_libraries(twoine_image_fs_tests PRIVATE twoine_runtime)
    add_dependencies(twoine_image_fs_tests twoine_ball)
    add_test(NAME image_drives COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/image_drives.py" --program $<TARGET_FILE:twoine_image_fs_tests> --work "${PROJECT_BINARY_DIR}/image-drive-tests")
    add_test(NAME filesystem_images COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/filesystem_images.py" --program $<TARGET_FILE:twoine_image_fs_tests> --work "${PROJECT_BINARY_DIR}/filesystem-image-tests")
    set_tests_properties(filesystem_images PROPERTIES TIMEOUT 60 ENVIRONMENT "TWOINE_BALL_LIBRARY=$<TARGET_FILE:twoine_ball>")
    add_test(NAME floppy_drives COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/floppy_drives.py" --program $<TARGET_FILE:twoine_image_fs_tests> --work "${PROJECT_BINARY_DIR}/floppy-drive-tests")
    set_tests_properties(floppy_drives PROPERTIES TIMEOUT 30 ENVIRONMENT "TWOINE_BALL_LIBRARY=$<TARGET_FILE:twoine_ball>")
    set_tests_properties(image_drives PROPERTIES TIMEOUT 30 ENVIRONMENT "TWOINE_BALL_LIBRARY=$<TARGET_FILE:twoine_ball>")
    add_executable(twoine_fatal_tests hal/test_fatal.c)
    target_link_libraries(twoine_fatal_tests PRIVATE twoine_runtime)
    add_executable(twoine_host_platform_tests hal/test_host_platform.c)
    target_link_libraries(twoine_host_platform_tests PRIVATE twoine_runtime)
    get_target_property(_twoine_host_platform twoine_portable_hal TWOINE_HOST_PLATFORM)
    target_compile_definitions(twoine_host_platform_tests PRIVATE TWOINE_TEST_HOST_OS="${_twoine_host_platform}")
    add_test(NAME host_platform COMMAND twoine_host_platform_tests "space argument" "" "a\\b")
    set_tests_properties(host_platform PROPERTIES TIMEOUT 20)
    if(_twoine_host_platform STREQUAL "linux")
        add_executable(twoine_host_owner_tests hal/linux/test_mutex_owner.c)
        target_link_libraries(twoine_host_owner_tests PRIVATE twoine_runtime)
        add_test(NAME host_mutex_owner COMMAND twoine_host_owner_tests)
        set_tests_properties(host_mutex_owner PROPERTIES TIMEOUT 20)
    endif()
    add_executable(twoine_host_sync_tests hal/test_host_sync.c)
    target_link_libraries(twoine_host_sync_tests PRIVATE twoine_runtime Threads::Threads)
    add_test(NAME host_sync COMMAND twoine_host_sync_tests)
    set_tests_properties(host_sync PROPERTIES TIMEOUT 20)
    if(UNIX)
        add_executable(twoine_operation_fixture hal/operation_fixture.c)
        target_link_libraries(twoine_operation_fixture PRIVATE twoine_runtime)
        add_test(NAME host_operation_lifecycle COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/operation_lifecycle.py"
            "$<TARGET_FILE:twoine_operation_fixture>")
        set_tests_properties(host_operation_lifecycle PROPERTIES TIMEOUT 45)
    endif()
    add_executable(twoine_handle_tests guest/test_handles.c ${PROJECT_SOURCE_DIR}/guest/handles.c)
    target_include_directories(twoine_handle_tests PRIVATE ${PROJECT_SOURCE_DIR}/guest)
    target_link_libraries(twoine_handle_tests PRIVATE twoine_runtime)
    add_test(NAME guest_handles COMMAND twoine_handle_tests)
    set_tests_properties(guest_handles PROPERTIES TIMEOUT 20)
    add_executable(twoine_execution_tests runtime/test_execution.c)
    target_link_libraries(twoine_execution_tests PRIVATE twoine_runtime)
    add_test(NAME runtime_execution COMMAND twoine_execution_tests unicorn)
    set_tests_properties(runtime_execution PROPERTIES TIMEOUT 20)
    add_executable(twoine_runtime_tests hal/test_cpu.c)
    target_link_libraries(twoine_runtime_tests PRIVATE twoine_runtime)
    add_test(NAME runtime_cpu COMMAND twoine_runtime_tests)
    set_tests_properties(runtime_cpu PROPERTIES TIMEOUT 20)

endif()
if(TWOINE_NATIVE_LOADER)
    add_executable(twoine_guest_abi_tests hal/linux/test_guest_abi.c)
    twoine_application_target(twoine_guest_abi_tests)
    add_test(NAME native_guest_abi COMMAND twoine_guest_abi_tests)
    add_executable(twoine_native_execution_tests runtime/test_execution.c)
    twoine_application_target(twoine_native_execution_tests)
    target_include_directories(twoine_native_execution_tests PRIVATE "${PROJECT_SOURCE_DIR}/runtime")
    target_link_libraries(twoine_native_execution_tests PRIVATE 2ine)
    add_test(NAME native_execution COMMAND twoine_native_execution_tests native)
    set_tests_properties(native_execution PROPERTIES TIMEOUT 20)
endif()
if(TARGET twoine_runtime)
    add_executable(twoine_socket_tests hal/test_socket.c)
    target_link_libraries(twoine_socket_tests PRIVATE twoine_runtime)
    add_test(NAME host_socket COMMAND twoine_socket_tests)
    set_tests_properties(host_socket PROPERTIES TIMEOUT 20)
    add_executable(twoine_diagnostic_tests hal/test_diagnostics.c)
    target_link_libraries(twoine_diagnostic_tests PRIVATE twoine_runtime)
    add_test(NAME host_diagnostics COMMAND twoine_diagnostic_tests)
    set_tests_properties(host_diagnostics PROPERTIES TIMEOUT 20)
    add_executable(twoine_network_tests hal/test_network.c ${PROJECT_SOURCE_DIR}/os2libs/network.c)
    target_link_libraries(twoine_network_tests PRIVATE twoine_runtime)
    add_test(NAME host_network COMMAND twoine_network_tests)
    set_tests_properties(host_network PROPERTIES TIMEOUT 20)
    find_program(TWOINE_OPENSSL_TOOL NAMES openssl HINTS "${OPENSSL_INCLUDE_DIR}/../bin" REQUIRED)
    add_executable(twoine_tls_tests hal/test_tls.c)
    target_link_libraries(twoine_tls_tests PRIVATE twoine_runtime Threads::Threads)
    add_test(NAME host_tls COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/host_tls.py"
        "$<TARGET_FILE:twoine_tls_tests>" "${TWOINE_OPENSSL_TOOL}")
    set_tests_properties(host_tls PROPERTIES TIMEOUT 40)
endif()
if(TARGET 2ine)
    add_executable(twoine_ssl_shim_tests hal/test_tls.c)
    twoine_application_target(twoine_ssl_shim_tests)
    target_compile_definitions(twoine_ssl_shim_tests PRIVATE TWOINE_TLS_TEST_SHIM=1)
    target_link_libraries(twoine_ssl_shim_tests PRIVATE ssleay32 2ine Threads::Threads)
    find_program(TWOINE_OPENSSL_TOOL NAMES openssl HINTS "${OPENSSL_INCLUDE_DIR}/../bin" REQUIRED)
    add_test(NAME os2_tls_shim COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/host_tls.py"
        "$<TARGET_FILE:twoine_ssl_shim_tests>" "${TWOINE_OPENSSL_TOOL}")
    set_tests_properties(os2_tls_shim PROPERTIES TIMEOUT 40)
    add_executable(twoine_capture_tests hal/test_capture.c)
    twoine_application_target(twoine_capture_tests)
    target_link_libraries(twoine_capture_tests PRIVATE 2ine SDL3::SDL3)
    add_test(NAME host_mouse_capture COMMAND twoine_capture_tests)
    set_tests_properties(host_mouse_capture PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software;TWOINE_MOUSE_CAPTURE=1")
    add_executable(twoine_media_tests hal/test_media.c)
    twoine_application_target(twoine_media_tests)
    target_link_libraries(twoine_media_tests PRIVATE 2ine)
    add_test(NAME host_media COMMAND twoine_media_tests)
    set_tests_properties(host_media PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
    add_executable(twoine_fullscreen_tests hal/test_fullscreen.c)
    twoine_application_target(twoine_fullscreen_tests)
    target_link_libraries(twoine_fullscreen_tests PRIVATE 2ine)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME host_fullscreen COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/fullscreen.py" --program $<TARGET_FILE:twoine_fullscreen_tests> --work "${PROJECT_BINARY_DIR}/fullscreen-results")
        set_tests_properties(host_fullscreen PROPERTIES TIMEOUT 40)
    endif()
endif()

if(TARGET viocalls)
    add_executable(twoine_terminal_tests hal/test_terminal.c)
    twoine_application_target(twoine_terminal_tests)
    target_link_libraries(twoine_terminal_tests PRIVATE viocalls)
    add_test(NAME host_terminal COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/hal/terminal_pty.py" $<TARGET_FILE:twoine_terminal_tests>)
    set_tests_properties(host_terminal PROPERTIES TIMEOUT 20)
endif()

if(TARGET sesmgr AND TARGET quecalls)
    add_executable(twoine_legacy_session_tests os2libs/test_legacy_sessions.c)
    twoine_application_target(twoine_legacy_session_tests)
    target_link_libraries(twoine_legacy_session_tests PRIVATE 2ine ${CMAKE_DL_LIBS})
    add_test(NAME legacy_sessions COMMAND twoine_legacy_session_tests $<TARGET_FILE:quecalls> $<TARGET_FILE:sesmgr>)
    set_tests_properties(legacy_sessions PROPERTIES TIMEOUT 20)
endif()

if(TARGET pmwin)
    if(APPLE AND TARGET pmwinx)
        add_library(twoine_cursor_backend SHARED hal/macos/cursor_backend.c)
        target_include_directories(twoine_cursor_backend PRIVATE "${PROJECT_SOURCE_DIR}")
        target_link_libraries(twoine_cursor_backend PRIVATE SDL3::SDL3)
        add_executable(twoine_cursor_resource_tests hal/macos/test_cursor_resources.c)
        twoine_application_target(twoine_cursor_resource_tests)
        target_link_libraries(twoine_cursor_resource_tests PRIVATE twoine_cursor_backend 2ine SDL3::SDL3)
        add_test(NAME cursor_resource_lifecycle COMMAND twoine_cursor_resource_tests $<TARGET_FILE:pmwinx> $<TARGET_FILE:pmwin>)
        set_tests_properties(cursor_resource_lifecycle PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software;DYLD_INSERT_LIBRARIES=$<TARGET_FILE:twoine_cursor_backend>")
    endif()
    add_executable(twoine_pm_tests os2libs/test_pm.c)
    add_executable(twoine_clipboard_pm_tests os2libs/test_clipboard.c)
    twoine_application_target(twoine_clipboard_pm_tests)
    target_link_libraries(twoine_clipboard_pm_tests PRIVATE pmwin 2ine SDL3::SDL3)
    add_test(NAME clipboard_pm COMMAND twoine_clipboard_pm_tests)
    set_tests_properties(clipboard_pm PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
    add_executable(twoine_clipboard_memory_hook_tests os2libs/test_clipboard_memory_hook.c)
    twoine_application_target(twoine_clipboard_memory_hook_tests)
    target_link_libraries(twoine_clipboard_memory_hook_tests PRIVATE 2ine)
    add_test(NAME clipboard_memory_hook COMMAND twoine_clipboard_memory_hook_tests $<TARGET_FILE:doscalls>)
    set_tests_properties(clipboard_memory_hook PROPERTIES TIMEOUT 10)
    twoine_application_target(twoine_pm_tests)
    target_link_libraries(twoine_pm_tests PRIVATE pmgpi pmwin 2ine)
    add_test(NAME presentation_manager COMMAND twoine_pm_tests)
    set_tests_properties(presentation_manager PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
    add_executable(twoine_help_font_tests os2libs/test_help_fonts.c)
    twoine_application_target(twoine_help_font_tests)
    target_link_libraries(twoine_help_font_tests PRIVATE pmgpi pmwin 2ine)
    add_test(NAME help_font_transitions COMMAND twoine_help_font_tests)
    set_tests_properties(help_font_transitions PROPERTIES TIMEOUT 20
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software;TWOINE_DISPLAY_DPI=96")
    add_executable(twoine_frame_format_tests os2libs/test_frame_format.c)
    twoine_application_target(twoine_frame_format_tests)
    target_link_libraries(twoine_frame_format_tests PRIVATE pmwin 2ine)
    add_test(NAME frame_format_protocol COMMAND twoine_frame_format_tests)
    set_tests_properties(frame_format_protocol PROPERTIES TIMEOUT 20
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
    add_executable(twoine_display_dpi_tests os2libs/test_display_dpi.c)
    twoine_application_target(twoine_display_dpi_tests)
    target_link_libraries(twoine_display_dpi_tests PRIVATE pmgpi pmwin 2ine)
    foreach(dpi 96 120 144 192)
        add_test(NAME display_dpi_${dpi} COMMAND twoine_display_dpi_tests ${dpi})
        set_tests_properties(display_dpi_${dpi} PROPERTIES TIMEOUT 20
            ENVIRONMENT "TWOINE_DISPLAY_DPI=${dpi};SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
    endforeach()
    add_executable(twoine_menu_chrome_tests os2libs/test_menu_chrome.c)
    twoine_application_target(twoine_menu_chrome_tests)
    target_link_libraries(twoine_menu_chrome_tests PRIVATE pmwin 2ine)
    add_test(NAME pm_menu_chrome COMMAND twoine_menu_chrome_tests)
    set_tests_properties(pm_menu_chrome PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
    add_executable(twoine_printing_tests os2libs/test_printing.c)
    twoine_application_target(twoine_printing_tests)
    target_link_libraries(twoine_printing_tests PRIVATE pmgpi pmwin pmshapi pmspl 2ine)
    add_test(NAME printer_output COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/printing.py" --build "${PROJECT_BINARY_DIR}"
        --backend "$<IF:$<BOOL:${TWOINE_PORTABLE_LOADER}>,unicorn,native>")
    set_tests_properties(printer_output PROPERTIES TIMEOUT 60
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
    add_executable(twoine_dialog_tests os2libs/test_dialogs.c)
    twoine_application_target(twoine_dialog_tests)
    target_link_libraries(twoine_dialog_tests PRIVATE pmwin 2ine)
    add_test(NAME pm_dialogs COMMAND twoine_dialog_tests)
    set_tests_properties(pm_dialogs PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
endif()

set(TWOINE_PMSTARS_EXE "" CACHE FILEPATH "Optional unchanged archived PMStars.exe fixture")
set(TWOINE_RUNBOX_EXE "" CACHE FILEPATH "Optional RunBox executable built from archived source")
if(TARGET lx_loader)
    if(TWOINE_PORTABLE_LOADER)
        add_test(NAME network_guest COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/network_guest.py"
            --build "${PROJECT_BINARY_DIR}" --openssl "${TWOINE_OPENSSL_TOOL}")
        set_tests_properties(network_guest PROPERTIES TIMEOUT 60)
        add_test(NAME network_diagnostics_guest COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/network_diagnostics.py"
            --build "${PROJECT_BINARY_DIR}")
        set_tests_properties(network_diagnostics_guest PROPERTIES TIMEOUT 45)
    endif()
    if(TWOINE_PORTABLE_LOADER)
        set(_twoine_legacy_backend unicorn)
    else()
        set(_twoine_legacy_backend native)
    endif()
    set(TWOINE_OS2_NETWORK_TOOLS "" CACHE PATH "Optional private MPTN directory with unchanged IBM bin and dll files")
    if(TWOINE_OS2_NETWORK_TOOLS)
        add_test(NAME os2_network_tools COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/network_diagnostics.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
            --os2-tools "${TWOINE_OS2_NETWORK_TOOLS}")
        set_tests_properties(os2_network_tools PROPERTIES TIMEOUT 75)
    endif()
    if(TARGET opengl)
        add_test(NAME opengl_guest COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/opengl.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
            --work "${PROJECT_BINARY_DIR}/opengl-results/guest")
        set_tests_properties(opengl_guest PROPERTIES TIMEOUT 180)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_program(TWOINE_XVFB_RUN xvfb-run)
            if(TWOINE_XVFB_RUN)
                add_test(NAME opengl_desktop_guest COMMAND "${TWOINE_XVFB_RUN}" -a
                    "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/opengl.py"
                    --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
                    --video-driver x11 --render-driver opengl
                    --work "${PROJECT_BINARY_DIR}/opengl-results/desktop")
                add_test(NAME opengl_desktop_pm COMMAND "${TWOINE_XVFB_RUN}" -a $<TARGET_FILE:twoine_opengl_pm_tests>)
                set_tests_properties(opengl_desktop_guest opengl_desktop_pm PROPERTIES TIMEOUT 180)
                set_tests_properties(opengl_desktop_pm PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=x11;SDL_RENDER_DRIVER=opengl;LIBGL_ALWAYS_SOFTWARE=1")
            endif()
        endif()
        set(TWOINE_OPENGL_SDK "" CACHE PATH "Optional OS/2 SDK fixture for SIMPLE.C")
        set(TWOINE_OPENGL_WATCOM "" CACHE PATH "Optional Linux-hosted Watcom fixture")
        if(TWOINE_OPENGL_SDK OR TWOINE_OPENGL_WATCOM)
            if(NOT TWOINE_OPENGL_SDK OR NOT TWOINE_OPENGL_WATCOM OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
                message(FATAL_ERROR "The SDK OpenGL fixture needs both TWOINE_OPENGL_SDK and TWOINE_OPENGL_WATCOM on Linux.")
            endif()
            add_test(NAME opengl_sdk COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/integration/opengl_sdk.py"
                --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
                --sdk "${TWOINE_OPENGL_SDK}" --watcom "${TWOINE_OPENGL_WATCOM}"
                --work "${PROJECT_BINARY_DIR}/opengl-results/sdk")
            set_tests_properties(opengl_sdk PROPERTIES TIMEOUT 90)
        endif()
    endif()
    add_test(NAME pm_dialog_apis COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/dialogs.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(pm_dialog_apis PROPERTIES TIMEOUT 20)
    add_test(NAME legacy_file_apis COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/legacy.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(legacy_file_apis PROPERTIES TIMEOUT 40)
    add_test(NAME lx_forwarded_exports COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/lx_exports.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(lx_forwarded_exports PROPERTIES TIMEOUT 30)
    add_test(NAME desktop_apis COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/desktop.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(desktop_apis PROPERTIES TIMEOUT 40)
    add_test(NAME mux_wait_apis COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/mux_wait.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(mux_wait_apis PROPERTIES TIMEOUT 40)
    add_test(NAME queue_apis COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/queues.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(queue_apis PROPERTIES TIMEOUT 40)
    add_test(NAME legacy_session_apis COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/legacy_sessions.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(legacy_session_apis PROPERTIES TIMEOUT 20)
    if(UNIX)
        add_test(NAME installer_operation_apis COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/installer_lifecycle.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
        set_tests_properties(installer_operation_apis PROPERTIES TIMEOUT 35)
    endif()
    add_test(NAME exception_apis COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/exceptions.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(exception_apis PROPERTIES TIMEOUT 20)
    add_test(NAME rexx_apis COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/rexx_apis.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(rexx_apis PROPERTIES TIMEOUT 60)
    add_test(NAME critical_section_apis COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/critical_sections.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(critical_section_apis PROPERTIES TIMEOUT 40)
    add_test(NAME titlebar_subclass_apis COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/titlebar.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(titlebar_subclass_apis PROPERTIES TIMEOUT 20)
    add_test(NAME open32_apis COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/open32.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(open32_apis PROPERTIES TIMEOUT 40)
    add_test(NAME fatal_report COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/fatal_report.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(fatal_report PROPERTIES TIMEOUT 120)
    add_test(NAME missing_import_exit COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/missing_import.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(missing_import_exit PROPERTIES TIMEOUT 40)
    add_test(NAME open32_semaphores COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/open32_semaphores.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(open32_semaphores PROPERTIES TIMEOUT 30)
    add_test(NAME clipboard_guest COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/clipboard.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(clipboard_guest PROPERTIES TIMEOUT 20)
    add_test(NAME pm_window_apis COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/windows.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(pm_window_apis PROPERTIES TIMEOUT 40)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET pmwin)
    if(TWOINE_PORTABLE_LOADER)
        set(_twoine_gui_backend unicorn)
    else()
        set(_twoine_gui_backend native)
    endif()
    foreach(profile pmstars runbox)
        string(TOUPPER "${profile}" _twoine_gui_name)
        if(TWOINE_${_twoine_gui_name}_EXE)
            if(NOT EXISTS "${TWOINE_${_twoine_gui_name}_EXE}")
                message(FATAL_ERROR "GUI fixture does not exist: ${TWOINE_${_twoine_gui_name}_EXE}")
            endif()
            find_program(TWOINE_XVFB_EXECUTABLE Xvfb REQUIRED)
            set(_twoine_gui_exit 0)
            if(profile STREQUAL "pmstars")
                # The archived program has void main and returns WinTerminate's TRUE value.
                set(_twoine_gui_exit 1)
            endif()
            add_test(NAME gui_${profile} COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/gui.py"
                --build "${PROJECT_BINARY_DIR}" --program "${TWOINE_${_twoine_gui_name}_EXE}"
                --backend "${_twoine_gui_backend}" --profile "${profile}" --expected-exit "${_twoine_gui_exit}"
                --work "${PROJECT_BINARY_DIR}/gui-results/${profile}")
            set_tests_properties(gui_${profile} PROPERTIES TIMEOUT 30)
        endif()
    endforeach()
endif()

if(TARGET pmwin AND TARGET pmgpi)
    add_executable(twoine_pm_display_startup_tests hal/test_pm_display_startup.c)
    twoine_application_target(twoine_pm_display_startup_tests)
    target_link_libraries(twoine_pm_display_startup_tests PRIVATE pmgpi pmwin 2ine SDL3::SDL3)
    foreach(_display_case gui retained vio compatible ne none invalid)
        add_test(NAME pm_display_startup_${_display_case} COMMAND twoine_pm_display_startup_tests
            $<TARGET_FILE:pmwin> ${_display_case})
        if(_display_case STREQUAL "invalid")
            set(_display_driver "not-a-video-driver")
        else()
            set(_display_driver "dummy")
        endif()
        set_tests_properties(pm_display_startup_${_display_case} PROPERTIES TIMEOUT 20
            ENVIRONMENT "SDL_VIDEO_DRIVER=${_display_driver};SDL_RENDER_DRIVER=software;SDL_AUDIO_DRIVER=dummy")
    endforeach()
endif()

if(TARGET pmwinx)
    add_executable(twoine_open32_atomic_tests os2libs/test_open32_atomic.c
        ../os2libs/pmwinx/atomic.c)
    twoine_application_target(twoine_open32_atomic_tests)
    target_link_libraries(twoine_open32_atomic_tests PRIVATE 2ine)
    add_test(NAME open32_atomic_backing COMMAND twoine_open32_atomic_tests)
    add_test(NAME open32_atomic COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/open32_atomic.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
        --work "${PROJECT_BINARY_DIR}/open32-atomic-tests")
    set_tests_properties(open32_atomic PROPERTIES TIMEOUT 30)
    add_executable(twoine_open32_adjust_window_tests os2libs/test_open32_adjust_window.c
        ../os2libs/pmwinx/adjust_window.c)
    twoine_application_target(twoine_open32_adjust_window_tests)
    add_test(NAME open32_adjust_window COMMAND twoine_open32_adjust_window_tests)
    add_executable(twoine_open32_load_menu_tests os2libs/test_open32_load_menu.c)
    twoine_application_target(twoine_open32_load_menu_tests)
    target_link_libraries(twoine_open32_load_menu_tests PRIVATE pmwinx pmwin pmgpi 2ine SDL3::SDL3)
    add_test(NAME open32_load_menu_ownership COMMAND twoine_open32_load_menu_tests)
    add_test(NAME open32_load_menu COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/open32_load_menu.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
        --work-dir "${PROJECT_BINARY_DIR}/open32-load-menu-tests")
    set_tests_properties(open32_load_menu PROPERTIES TIMEOUT 30)
    add_executable(twoine_settings_tests os2libs/test_settings.c)
    twoine_application_target(twoine_settings_tests)
    target_link_libraries(twoine_settings_tests PRIVATE pmwin pmwinx pmshapi pmwp helpmgr 2ine)
    add_test(NAME application_settings COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/os2libs/settings.py" $<TARGET_FILE:twoine_settings_tests>)
    set_tests_properties(application_settings PROPERTIES TIMEOUT 40)
endif()

if(TARGET pmshapi AND UNIX)
    add_executable(twoine_profile_worker os2libs/profile_worker.c)
    twoine_application_target(twoine_profile_worker)
    target_link_libraries(twoine_profile_worker PRIVATE pmshapi 2ine)
    add_test(NAME profile_concurrency COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/os2libs/profile_concurrency.py" $<TARGET_FILE:twoine_profile_worker>)
    set_tests_properties(profile_concurrency PROPERTIES TIMEOUT 45)
endif()

if(TARGET dive)
    add_executable(twoine_dive_tests os2libs/test_dive.c)
    add_executable(twoine_dive_format_tests os2libs/test_dive_formats.c)
    twoine_application_target(twoine_dive_format_tests)
    target_link_libraries(twoine_dive_format_tests PRIVATE dive)
    add_test(NAME dive_formats COMMAND twoine_dive_format_tests)
    twoine_application_target(twoine_dive_tests)
    target_link_libraries(twoine_dive_tests PRIVATE dive pmwin 2ine)
    add_test(NAME dive_video COMMAND twoine_dive_tests)
    set_tests_properties(dive_video PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
endif()
if(TARGET so32dll)
    add_executable(twoine_legacy_socket_tests os2libs/test_legacy_socket.c)
    twoine_application_target(twoine_legacy_socket_tests)
    target_link_libraries(twoine_legacy_socket_tests PRIVATE so32dll tcp32dll 2ine)
    add_test(NAME legacy_socket COMMAND twoine_legacy_socket_tests)
    set_tests_properties(legacy_socket PROPERTIES TIMEOUT 20)
endif()
if(TARGET mdm)
    add_executable(twoine_mdm_tests os2libs/test_mdm.c)
    twoine_application_target(twoine_mdm_tests)
    target_link_libraries(twoine_mdm_tests PRIVATE mdm mmio 2ine m)
    add_test(NAME multimedia COMMAND twoine_mdm_tests)
    set_tests_properties(multimedia PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
    add_test(NAME multimedia_stereo_disk COMMAND twoine_mdm_tests)
    set_tests_properties(multimedia_stereo_disk PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=disk;SDL_RENDER_DRIVER=software;SDL_AUDIO_DISK_OUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/mdm-stereo.raw;TWOINE_TEST_STEREO_CAPTURE=${CMAKE_CURRENT_BINARY_DIR}/mdm-stereo.raw")
endif()

if(TARGET doscalls)
    add_executable(twoine_file_compat_tests os2libs/test_file_compat.c)
    twoine_application_target(twoine_file_compat_tests)
    target_link_libraries(twoine_file_compat_tests PRIVATE 2ine ${CMAKE_DL_LIBS})
    add_test(NAME file_compat COMMAND twoine_file_compat_tests $<TARGET_FILE:doscalls>)
    set_tests_properties(file_compat PROPERTIES TIMEOUT 20)
    add_executable(twoine_legacy_core_tests os2libs/test_legacy_core.c)
    twoine_application_target(twoine_legacy_core_tests)
    target_link_libraries(twoine_legacy_core_tests PRIVATE 2ine ${CMAKE_DL_LIBS})
    add_test(NAME legacy_core COMMAND twoine_legacy_core_tests $<TARGET_FILE:doscalls>)
    set_tests_properties(legacy_core PROPERTIES TIMEOUT 20)
endif()

if(TARGET dive)
    add_executable(twoine_visibility_tests os2libs/test_visibility.c)
    twoine_application_target(twoine_visibility_tests)
    target_link_libraries(twoine_visibility_tests PRIVATE pmgpi pmwin 2ine)
    add_test(NAME pm_visible_regions COMMAND twoine_visibility_tests)
    set_tests_properties(pm_visible_regions PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
    add_test(NAME dive_guest_apis COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/dive.py" --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
endif()

if(TARGET pmwin)
    add_executable(twoine_pm_thread_tests os2libs/test_pm_threads.c)
    twoine_application_target(twoine_pm_thread_tests)
    target_link_libraries(twoine_pm_thread_tests PRIVATE pmwin 2ine)
    add_test(NAME pm_thread_posts COMMAND twoine_pm_thread_tests)
    set_tests_properties(pm_thread_posts PROPERTIES TIMEOUT 20 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
endif()

if(TARGET doscalls)
    add_test(NAME guest_file_compat COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/file_compat.py" --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(guest_file_compat PROPERTIES TIMEOUT 20)
endif()

if(TARGET mdm AND TWOINE_MIDI_SYNTH)
    add_executable(twoine_midi_synth_tests os2libs/test_midi_synth.c ../os2libs/midi_synth.c ../os2libs/midi_config.c ../os2libs/midi_mt32.c)
    twoine_application_target(twoine_midi_synth_tests)
    target_link_libraries(twoine_midi_synth_tests PRIVATE twoine_midi_config twoine_mt32_config 2ine m)
    if(TWOINE_MIDI_EXTENDED)
        target_sources(twoine_midi_synth_tests PRIVATE ../os2libs/midi_banks.cpp)
        target_compile_features(twoine_midi_synth_tests PRIVATE cxx_std_17)
        add_executable(twoine_midi_bank_tests os2libs/test_midi_banks.c ../os2libs/midi_synth.c ../os2libs/midi_config.c ../os2libs/midi_mt32.c ../os2libs/midi_banks.cpp)
        twoine_application_target(twoine_midi_bank_tests)
        target_compile_features(twoine_midi_bank_tests PRIVATE cxx_std_17)
        target_link_libraries(twoine_midi_bank_tests PRIVATE twoine_midi_config twoine_mt32_config 2ine m)
        add_test(NAME midi_bank_formats COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/integration/midi_banks.py" --build "${PROJECT_BINARY_DIR}")
        set_tests_properties(midi_bank_formats PROPERTIES TIMEOUT 120)

    endif()
    add_test(NAME midi_instruments COMMAND twoine_midi_synth_tests)
    set_tests_properties(midi_instruments PROPERTIES TIMEOUT 30
        ENVIRONMENT "TWOINE_SOUNDFONT=${PROJECT_BINARY_DIR}/soundfonts/FluidR3_GM.sf2;TWOINE_AUDIO_CONFIG=${PROJECT_BINARY_DIR}/no-user-audio.conf")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET dive)
    set(TWOINE_DOOM_EXE "" CACHE FILEPATH "Separately supplied source-built OS/2 Doom executable")
    set(TWOINE_DOOM_WAD "" CACHE FILEPATH "Separately supplied Doom shareware WAD")
    if(TWOINE_DOOM_EXE AND TWOINE_DOOM_WAD)
        add_test(NAME doom_gameplay COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/doom.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
            --program "${TWOINE_DOOM_EXE}" --wad "${TWOINE_DOOM_WAD}"
            --source-built --work "${PROJECT_BINARY_DIR}/doom-results" --seconds 70)
        set_tests_properties(doom_gameplay PROPERTIES TIMEOUT 150)
    endif()
endif()

if(TARGET mdm AND UNIX)
    add_executable(twoine_midi_config_tests os2libs/test_midi_config.c ../os2libs/midi_config.c)
    twoine_application_target(twoine_midi_config_tests)
    target_link_libraries(twoine_midi_config_tests PRIVATE 2ine)
    add_test(NAME midi_preferences COMMAND twoine_midi_config_tests)
    set_tests_properties(midi_preferences PROPERTIES TIMEOUT 20)
endif()

if(TARGET mdm AND UNIX)
    add_executable(twoine_midi_mt32_tests os2libs/test_midi_mt32.c ../os2libs/midi_mt32.c ../os2libs/midi_config.c)
    twoine_application_target(twoine_midi_mt32_tests)
    target_link_libraries(twoine_midi_mt32_tests PRIVATE twoine_mt32_config 2ine m)
    add_test(NAME midi_mt32_availability COMMAND twoine_midi_mt32_tests)
    set_tests_properties(midi_mt32_availability PROPERTIES TIMEOUT 20)
endif()

if(TARGET pmwinx)
    add_test(NAME image_install COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/image_install.py" --build "${PROJECT_BINARY_DIR}" --work "${PROJECT_BINARY_DIR}/image-install-tests")
    set_tests_properties(image_install PROPERTIES TIMEOUT 30 ENVIRONMENT "TWOINE_BALL_LIBRARY=$<TARGET_FILE:twoine_ball>")
    add_test(NAME ball COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/ball.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(ball PROPERTIES TIMEOUT 60)
endif()
if(TARGET quecalls)
    add_test(NAME installer_apis COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/install_apis.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(installer_apis PROPERTIES TIMEOUT 60)
endif()
if(TARGET 2ine)
    add_executable(twoine_shared_lifetime_tests hal/test_shared_lifetime.c)
    twoine_application_target(twoine_shared_lifetime_tests)
    target_link_libraries(twoine_shared_lifetime_tests PRIVATE 2ine)
    add_test(NAME shared_lifetime COMMAND twoine_shared_lifetime_tests)
    set_tests_properties(shared_lifetime PROPERTIES TIMEOUT 20)
endif()

if(TARGET kbdcalls AND TARGET viocalls AND TARGET nls)
    add_executable(twoine_console_compat_tests os2libs/test_console_compat.c)
    twoine_application_target(twoine_console_compat_tests)
    target_link_libraries(twoine_console_compat_tests PRIVATE 2ine ${CMAKE_DL_LIBS})
    add_test(NAME console_compat COMMAND twoine_console_compat_tests $<TARGET_FILE:kbdcalls> $<TARGET_FILE:viocalls> $<TARGET_FILE:nls>)
    set_tests_properties(console_compat PROPERTIES TIMEOUT 20)
endif()

if(TARGET pmwinx)
    add_executable(twoine_open32_unicode_tests os2libs/test_open32_unicode.c
        ../os2libs/pmwinx/unicode.c)
    twoine_application_target(twoine_open32_unicode_tests)
    target_link_libraries(twoine_open32_unicode_tests PRIVATE 2ine)
    add_test(NAME open32_unicode_conversion COMMAND twoine_open32_unicode_tests)
    add_test(NAME open32_unicode COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/open32_unicode.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
        --work "${PROJECT_BINARY_DIR}/open32-unicode-tests")
    set_tests_properties(open32_unicode PROPERTIES TIMEOUT 45)
    add_executable(twoine_open32_menu_metric_tests os2libs/test_open32_menu_metrics.c
        ../os2libs/pmwinx/metrics.c)
    twoine_application_target(twoine_open32_menu_metric_tests)
    add_test(NAME open32_menu_metric_dimensions COMMAND twoine_open32_menu_metric_tests)
    add_test(NAME open32_menu_metrics COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/open32_menu_metrics.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
        --work-dir "${PROJECT_BINARY_DIR}/open32-menu-metric-tests")
    set_tests_properties(open32_menu_metrics PROPERTIES TIMEOUT 30)
    add_executable(twoine_open32_pointer_tests os2libs/test_open32_pointers.c
        ../os2libs/pmwinx/pointers.c)
    twoine_application_target(twoine_open32_pointer_tests)
    add_test(NAME open32_pointer_coordinates COMMAND twoine_open32_pointer_tests)
    add_test(NAME open32_pointers COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/open32_pointers.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
        --work-dir "${PROJECT_BINARY_DIR}/open32-pointer-tests")
    set_tests_properties(open32_pointers PROPERTIES TIMEOUT 30)
    add_executable(twoine_open32_priority_tests os2libs/test_open32_priority.c
        ../os2libs/pmwinx/priority.c)
    twoine_application_target(twoine_open32_priority_tests)
    target_link_libraries(twoine_open32_priority_tests PRIVATE 2ine)
    add_test(NAME open32_priority_mapping COMMAND twoine_open32_priority_tests)
    add_test(NAME open32_priority COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/open32_priority.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
        --work "${PROJECT_BINARY_DIR}/open32-priority-tests")
    set_tests_properties(open32_priority PROPERTIES TIMEOUT 30)
    if(APPLE)
        add_executable(twoine_open32_priority_host_tests os2libs/test_open32_priority.c
            ../os2libs/pmwinx/priority.c)
        twoine_application_target(twoine_open32_priority_host_tests)
        target_compile_definitions(twoine_open32_priority_host_tests PRIVATE TWOINE_PRIORITY_REAL_HOST=1)
        target_link_libraries(twoine_open32_priority_host_tests PRIVATE 2ine)
        add_test(NAME open32_priority_host COMMAND twoine_open32_priority_host_tests)
    endif()
    if(UNIX)
        add_executable(twoine_process_priority_linux_tests hal/test_process_priority_linux.c
            ../hal/linux/host_process.c)
        twoine_application_target(twoine_process_priority_linux_tests)
        target_compile_definitions(twoine_process_priority_linux_tests PRIVATE
            opendir=priority_test_opendir readdir=priority_test_readdir
            closedir=priority_test_closedir getpriority=priority_test_getpriority
            setpriority=priority_test_setpriority)
        add_test(NAME process_priority_linux_policy COMMAND twoine_process_priority_linux_tests)
    endif()
    add_test(NAME open32_accelerators COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/open32_accelerators.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(open32_accelerators PROPERTIES TIMEOUT 30)
    add_executable(twoine_open32_accelerator_tests os2libs/test_open32_accelerators.c)
    twoine_application_target(twoine_open32_accelerator_tests)
    target_link_libraries(twoine_open32_accelerator_tests PRIVATE pmwinx 2ine SDL3::SDL3)
    add_test(NAME open32_accelerator_ownership COMMAND twoine_open32_accelerator_tests $<TARGET_FILE:pmwinx>)
    set_tests_properties(open32_accelerator_ownership PROPERTIES TIMEOUT 20)
    add_test(NAME open32_tls COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/open32_tls.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(open32_tls PROPERTIES TIMEOUT 30)
    add_executable(twoine_open32_tls_cleanup_tests os2libs/test_open32_tls_cleanup.c)
    twoine_application_target(twoine_open32_tls_cleanup_tests)
    target_link_libraries(twoine_open32_tls_cleanup_tests PRIVATE 2ine ${CMAKE_DL_LIBS})
    add_test(NAME open32_tls_cleanup COMMAND twoine_open32_tls_cleanup_tests $<TARGET_FILE:pmwinx>)
    set_tests_properties(open32_tls_cleanup PROPERTIES TIMEOUT 20)
endif()

if(TARGET doscalls)
    add_test(NAME allocation_alignment COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/allocation_alignment.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(allocation_alignment PROPERTIES TIMEOUT 45)
    add_test(NAME shared_memory_apis COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/shared_memory.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(shared_memory_apis PROPERTIES TIMEOUT 60)
endif()

if(TARGET doscalls)
    add_test(NAME named_mutex_apis COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/named_mutex.py"
        --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}")
    set_tests_properties(named_mutex_apis PROPERTIES TIMEOUT 60)
endif()

if(TARGET lx_loader)
    add_test(NAME loader_startup COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/integration/loader_startup.py"
        --build "${PROJECT_BINARY_DIR}")
    set_tests_properties(loader_startup PROPERTIES TIMEOUT 10)
endif()

if(TARGET pmmerge AND TARGET pmgpi)
    add_executable(twoine_help_api_tests os2libs/test_help_apis.c)
    twoine_application_target(twoine_help_api_tests)
    target_link_libraries(twoine_help_api_tests PRIVATE pmwin pmgpi pmmerge 2ine)
    add_test(NAME help_prerequisite_apis COMMAND twoine_help_api_tests)
    set_tests_properties(help_prerequisite_apis PROPERTIES TIMEOUT 20
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
endif()

if(TARGET mdm AND TWOINE_VIDEO)
    add_executable(twoine_video_tests os2libs/test_video.c)
    twoine_application_target(twoine_video_tests)
    target_link_libraries(twoine_video_tests PRIVATE mdm pmwin 2ine)
    add_test(NAME digital_video COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/video.py"
        --build "${PROJECT_BINARY_DIR}" --work "${PROJECT_BINARY_DIR}/video-tests"
        --backend "${_twoine_legacy_backend}")
    set_tests_properties(digital_video PROPERTIES TIMEOUT 40)
    add_test(NAME digital_video_audio COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/video.py"
        --build "${PROJECT_BINARY_DIR}" --work "${PROJECT_BINARY_DIR}/video-audio-tests"
        --backend "${_twoine_legacy_backend}" --audio-driver disk)
    set_tests_properties(digital_video_audio PROPERTIES TIMEOUT 40)
endif()

if(TARGET lx_loader AND TARGET twoine_ball_cli)
    add_test(NAME runtime_features COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/runtime_features.py"
        --build "${PROJECT_BINARY_DIR}" --work "${PROJECT_BINARY_DIR}/runtime-feature-tests"
        --backend "${_twoine_legacy_backend}")
    set_tests_properties(runtime_features PROPERTIES TIMEOUT 40)
    add_test(NAME ball_relocation COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/ball_relocation.py"
        --build "${PROJECT_BINARY_DIR}" --work "${PROJECT_BINARY_DIR}/ball-relocation-tests"
        --backend "${_twoine_legacy_backend}")
    set_tests_properties(ball_relocation PROPERTIES TIMEOUT 40)
    if(APPLE)
        add_test(NAME macos_native_cache COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/integration/macos_native_cache.py"
            --build "${PROJECT_BINARY_DIR}" --work "${PROJECT_BINARY_DIR}/macos-native-cache-tests")
        set_tests_properties(macos_native_cache PROPERTIES TIMEOUT 60)
    endif()
endif()

if(TARGET pmgpi AND TARGET pmwin)
    add_executable(twoine_bitmap_bits_tests os2libs/test_bitmap_bits.c)
    twoine_application_target(twoine_bitmap_bits_tests)
    target_link_libraries(twoine_bitmap_bits_tests PRIVATE pmgpi pmwin 2ine)
    add_test(NAME bitmap_bits COMMAND twoine_bitmap_bits_tests)
    set_tests_properties(bitmap_bits PROPERTIES TIMEOUT 15
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
endif()
if(TARGET lx_loader AND TARGET pmgpi)
    add_test(NAME bitmap_bits_guest COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/integration/bitmap_bits.py"
        --build "${PROJECT_BINARY_DIR}" --work "${PROJECT_BINARY_DIR}/bitmap-bits-tests"
        --backend "${_twoine_legacy_backend}")
    set_tests_properties(bitmap_bits_guest PROPERTIES TIMEOUT 30)
endif()
