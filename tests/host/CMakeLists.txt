# Public domain. No rights reserved. Wholly AI-authored.
# Host services: desktop, graphics, input, files, processes, sockets, TLS and terminals.
if(UNIX)
    add_executable(
        twoine_desktop_group_tests
        test_desktop_group.c
        ${PROJECT_SOURCE_DIR}/host/core/desktop/group.c
        ${PROJECT_SOURCE_DIR}/host/posix/desktop/lease.c
    )
    target_include_directories(twoine_desktop_group_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_compile_features(twoine_desktop_group_tests PRIVATE c_std_99)
    add_test(NAME desktop_group COMMAND twoine_desktop_group_tests)
    set_tests_properties(desktop_group PROPERTIES TIMEOUT 15)
    add_executable(
        twoine_desktop_cycle_tests
        test_desktop_cycle.c
        ${PROJECT_SOURCE_DIR}/host/core/desktop/cycle_order.c
        ${PROJECT_SOURCE_DIR}/host/posix/desktop/cycle_registry.c
        ${PROJECT_SOURCE_DIR}/host/posix/desktop/lease.c
    )
    target_include_directories(twoine_desktop_cycle_tests PRIVATE "${PROJECT_SOURCE_DIR}")
    target_compile_features(twoine_desktop_cycle_tests PRIVATE c_std_99)
    add_test(NAME desktop_cycle COMMAND twoine_desktop_cycle_tests)
    set_tests_properties(desktop_cycle PROPERTIES TIMEOUT 15)
endif()
if(TARGET opengl)
    add_executable(twoine_opengl_tests test_opengl.c)
    twoine_application_target(twoine_opengl_tests)
    target_link_libraries(twoine_opengl_tests PRIVATE opengl)
    add_test(NAME opengl_mesa COMMAND twoine_opengl_tests)
    set_tests_properties(opengl_mesa PROPERTIES TIMEOUT 60)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_tests_properties(opengl_mesa PROPERTIES ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
        add_executable(
            twoine_gl_policy_tests
            test_gl_policy.c
            ${PROJECT_SOURCE_DIR}/host/macos/opengl/driver_policy.c
        )
        twoine_application_target(twoine_gl_policy_tests)
        target_compile_definitions(
            twoine_gl_policy_tests
            PRIVATE TWOINE_KOSMICKRISP_ICD="/twoine-test/kosmickrisp.json"
        )
        add_test(NAME opengl_macos_policy COMMAND twoine_gl_policy_tests)
    endif()
endif()
if(TARGET 2ine)
    add_executable(twoine_clipboard_host_tests test_clipboard.c)
    twoine_application_target(twoine_clipboard_host_tests)
    target_link_libraries(twoine_clipboard_host_tests PRIVATE 2ine SDL3::SDL3)
    add_test(NAME clipboard_host COMMAND twoine_clipboard_host_tests)
    set_tests_properties(clipboard_host PROPERTIES TIMEOUT 15 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
endif()
if(TARGET twoine_runtime)
    add_executable(twoine_image_fs_tests test_image_fs.c)
    target_link_libraries(twoine_image_fs_tests PRIVATE twoine_runtime)
    add_dependencies(twoine_image_fs_tests twoine_ball)
    add_test(
        NAME image_drives
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/ball/image_drives.py"
            --program $<TARGET_FILE:twoine_image_fs_tests> --work
            "${PROJECT_BINARY_DIR}/image-drive-tests"
    )
    add_test(
        NAME filesystem_images
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/ball/filesystem_images.py" --program
            $<TARGET_FILE:twoine_image_fs_tests> --work
            "${PROJECT_BINARY_DIR}/filesystem-image-tests"
    )
    set_tests_properties(
        filesystem_images
        PROPERTIES TIMEOUT 60 ENVIRONMENT "TWOINE_BALL_LIBRARY=$<TARGET_FILE:twoine_ball>"
    )
    add_test(
        NAME floppy_drives
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/ball/floppy_drives.py"
            --program $<TARGET_FILE:twoine_image_fs_tests> --work
            "${PROJECT_BINARY_DIR}/floppy-drive-tests"
    )
    set_tests_properties(
        floppy_drives
        PROPERTIES TIMEOUT 30 ENVIRONMENT "TWOINE_BALL_LIBRARY=$<TARGET_FILE:twoine_ball>"
    )
    set_tests_properties(
        image_drives
        PROPERTIES TIMEOUT 30 ENVIRONMENT "TWOINE_BALL_LIBRARY=$<TARGET_FILE:twoine_ball>"
    )
    add_executable(twoine_fatal_tests test_fatal.c)
    target_link_libraries(twoine_fatal_tests PRIVATE twoine_runtime)
    add_executable(twoine_host_platform_tests test_host_platform.c)
    target_link_libraries(twoine_host_platform_tests PRIVATE twoine_runtime)
    get_target_property(_twoine_host_platform twoine_portable_host TWOINE_HOST_PLATFORM)
    target_compile_definitions(
        twoine_host_platform_tests
        PRIVATE TWOINE_TEST_HOST_OS="${_twoine_host_platform}"
    )
    add_test(NAME host_platform COMMAND twoine_host_platform_tests "space argument" "" "a\\b")
    set_tests_properties(host_platform PROPERTIES TIMEOUT 20)
    if(_twoine_host_platform STREQUAL "linux")
        add_executable(twoine_host_owner_tests linux/test_mutex_owner.c)
        target_link_libraries(twoine_host_owner_tests PRIVATE twoine_runtime)
        add_test(NAME host_mutex_owner COMMAND twoine_host_owner_tests)
        set_tests_properties(host_mutex_owner PROPERTIES TIMEOUT 20)
    endif()
    add_executable(twoine_host_sync_tests test_host_sync.c)
    target_link_libraries(twoine_host_sync_tests PRIVATE twoine_runtime Threads::Threads)
    add_test(NAME host_sync COMMAND twoine_host_sync_tests)
    set_tests_properties(host_sync PROPERTIES TIMEOUT 20)
    if(UNIX)
        add_executable(twoine_operation_fixture operation_fixture.c)
        target_link_libraries(twoine_operation_fixture PRIVATE twoine_runtime)
        add_test(
            NAME host_operation_lifecycle
            COMMAND
                "${Python3_EXECUTABLE}"
                "${TWOINE_TEST_SOURCE_DIR}/integration/emulator/operation_lifecycle.py"
                "$<TARGET_FILE:twoine_operation_fixture>"
        )
        set_tests_properties(host_operation_lifecycle PROPERTIES TIMEOUT 45)
    endif()
    add_executable(twoine_runtime_tests test_cpu.c)
    target_link_libraries(twoine_runtime_tests PRIVATE twoine_runtime)
    add_test(NAME runtime_cpu COMMAND twoine_runtime_tests)
    set_tests_properties(runtime_cpu PROPERTIES TIMEOUT 20)
endif()
if(TWOINE_NATIVE_LOADER)
    add_executable(twoine_guest_abi_tests linux/test_guest_abi.c)
    twoine_application_target(twoine_guest_abi_tests)
    add_test(NAME native_guest_abi COMMAND twoine_guest_abi_tests)
endif()
if(TARGET twoine_runtime)
    add_executable(twoine_socket_tests test_socket.c)
    target_link_libraries(twoine_socket_tests PRIVATE twoine_runtime)
    add_test(NAME host_socket COMMAND twoine_socket_tests)
    set_tests_properties(host_socket PROPERTIES TIMEOUT 20)
    add_executable(twoine_diagnostic_tests test_diagnostics.c)
    target_link_libraries(twoine_diagnostic_tests PRIVATE twoine_runtime)
    add_test(NAME host_diagnostics COMMAND twoine_diagnostic_tests)
    set_tests_properties(host_diagnostics PROPERTIES TIMEOUT 20)
    find_program(TWOINE_OPENSSL_TOOL NAMES openssl HINTS "${OPENSSL_INCLUDE_DIR}/../bin" REQUIRED)
    add_executable(twoine_tls_tests test_tls.c)
    target_link_libraries(twoine_tls_tests PRIVATE twoine_runtime Threads::Threads)
    add_test(
        NAME host_tls
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/host/host_tls.py"
            "$<TARGET_FILE:twoine_tls_tests>" "${TWOINE_OPENSSL_TOOL}"
    )
    set_tests_properties(host_tls PROPERTIES TIMEOUT 40)
endif()
if(TARGET 2ine)
    add_executable(twoine_ssl_shim_tests test_tls.c)
    twoine_application_target(twoine_ssl_shim_tests)
    target_compile_definitions(twoine_ssl_shim_tests PRIVATE TWOINE_TLS_TEST_SHIM=1)
    target_link_libraries(twoine_ssl_shim_tests PRIVATE ssleay32 2ine Threads::Threads)
    find_program(TWOINE_OPENSSL_TOOL NAMES openssl HINTS "${OPENSSL_INCLUDE_DIR}/../bin" REQUIRED)
    add_test(
        NAME os2_tls_shim
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/host/host_tls.py"
            "$<TARGET_FILE:twoine_ssl_shim_tests>" "${TWOINE_OPENSSL_TOOL}"
    )
    set_tests_properties(os2_tls_shim PROPERTIES TIMEOUT 40)
    add_executable(twoine_capture_tests test_capture.c)
    twoine_application_target(twoine_capture_tests)
    target_link_libraries(twoine_capture_tests PRIVATE 2ine SDL3::SDL3)
    add_test(NAME host_mouse_capture COMMAND twoine_capture_tests)
    set_tests_properties(
        host_mouse_capture
        PROPERTIES
            TIMEOUT 20
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software;TWOINE_MOUSE_CAPTURE=1"
    )
    add_executable(twoine_media_tests test_media.c)
    twoine_application_target(twoine_media_tests)
    target_link_libraries(twoine_media_tests PRIVATE 2ine)
    add_test(NAME host_media COMMAND twoine_media_tests)
    set_tests_properties(
        host_media
        PROPERTIES
            TIMEOUT 20
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
    )
    add_executable(twoine_fullscreen_tests test_fullscreen.c)
    twoine_application_target(twoine_fullscreen_tests)
    target_link_libraries(twoine_fullscreen_tests PRIVATE 2ine)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(
            NAME host_fullscreen
            COMMAND
                "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/host/fullscreen.py"
                --program $<TARGET_FILE:twoine_fullscreen_tests> --work
                "${PROJECT_BINARY_DIR}/fullscreen-results"
        )
        set_tests_properties(host_fullscreen PROPERTIES TIMEOUT 40)
    endif()
endif()

if(TARGET doscalls)
    add_executable(twoine_terminal_tests test_terminal.c)
    twoine_application_target(twoine_terminal_tests)
    target_link_libraries(twoine_terminal_tests PRIVATE doscalls)
    add_test(
        NAME host_terminal
        COMMAND
            "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/terminal_pty.py"
            $<TARGET_FILE:twoine_terminal_tests>
    )
    set_tests_properties(host_terminal PROPERTIES TIMEOUT 20)
endif()

if(TARGET pmmerge)
    if(APPLE AND TARGET pmwinx)
        add_library(twoine_cursor_backend SHARED macos/cursor_backend.c)
        target_include_directories(twoine_cursor_backend PRIVATE "${PROJECT_SOURCE_DIR}")
        target_link_libraries(twoine_cursor_backend PRIVATE SDL3::SDL3)
        add_executable(twoine_cursor_resource_tests macos/test_cursor_resources.c)
        twoine_application_target(twoine_cursor_resource_tests)
        target_link_libraries(
            twoine_cursor_resource_tests
            PRIVATE twoine_cursor_backend 2ine SDL3::SDL3
        )
        add_test(
            NAME cursor_resource_lifecycle
            COMMAND twoine_cursor_resource_tests $<TARGET_FILE:pmwinx> $<TARGET_FILE:pmmerge>
        )
        set_tests_properties(
            cursor_resource_lifecycle
            PROPERTIES
                TIMEOUT 20
                ENVIRONMENT
                    "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software;DYLD_INSERT_LIBRARIES=$<TARGET_FILE:twoine_cursor_backend>"
        )
    endif()
endif()

if(TARGET pmmerge AND TARGET pmgpi)
    add_executable(twoine_pm_display_startup_tests test_pm_display_startup.c)
    twoine_application_target(twoine_pm_display_startup_tests)
    target_link_libraries(twoine_pm_display_startup_tests PRIVATE pmgpi pmmerge 2ine SDL3::SDL3)
    foreach(
        _display_case
        gui
        retained
        vio
        compatible
        ne
        none
        invalid
    )
        add_test(
            NAME pm_display_startup_${_display_case}
            COMMAND twoine_pm_display_startup_tests $<TARGET_FILE:pmmerge> ${_display_case}
        )
        if(_display_case STREQUAL "invalid")
            set(_display_driver "not-a-video-driver")
        else()
            set(_display_driver "dummy")
        endif()
        set_tests_properties(
            pm_display_startup_${_display_case}
            PROPERTIES
                TIMEOUT 20
                ENVIRONMENT
                    "SDL_VIDEO_DRIVER=${_display_driver};SDL_RENDER_DRIVER=software;SDL_AUDIO_DRIVER=dummy"
        )
    endforeach()
endif()

if(TARGET 2ine)
    add_executable(twoine_shared_lifetime_tests test_shared_lifetime.c)
    twoine_application_target(twoine_shared_lifetime_tests)
    target_link_libraries(twoine_shared_lifetime_tests PRIVATE 2ine)
    add_test(NAME shared_lifetime COMMAND twoine_shared_lifetime_tests)
    set_tests_properties(shared_lifetime PROPERTIES TIMEOUT 20)
endif()

if(TARGET pmwinx)
    if(UNIX)
        add_executable(
            twoine_process_priority_linux_tests
            test_process_priority_linux.c
            ${PROJECT_SOURCE_DIR}/host/linux/process/self.c
        )
        twoine_application_target(twoine_process_priority_linux_tests)
        target_compile_definitions(
            twoine_process_priority_linux_tests
            PRIVATE
                opendir=priority_test_opendir
                readdir=priority_test_readdir
                closedir=priority_test_closedir
                getpriority=priority_test_getpriority
                setpriority=priority_test_setpriority
        )
        add_test(NAME process_priority_linux_policy COMMAND twoine_process_priority_linux_tests)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # A fake helper stands in for 2ine-dialog, so the protocol is tested
        # without a desktop session.
        add_executable(
            twoine_host_dialog_linux_tests
            linux/test_dialog_helper.c
            ${PROJECT_SOURCE_DIR}/host/linux/dialog/helper.c
            ${PROJECT_SOURCE_DIR}/host/linux/print/dialog.c
        )
        target_include_directories(
            twoine_host_dialog_linux_tests
            PRIVATE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/host/include"
        )
        add_test(NAME host_dialog_linux_protocol COMMAND twoine_host_dialog_linux_tests)
        set_tests_properties(
            host_dialog_linux_protocol
            PROPERTIES TIMEOUT 20 WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        )
    endif()
endif()
