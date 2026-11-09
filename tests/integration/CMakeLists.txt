# Public domain. No rights reserved. Wholly AI-authored.
# Generated and original OS/2 programs run through lx_loader, by the
# component they exercise (see the directories below integration/).
set(TWOINE_PMSTARS_EXE "" CACHE FILEPATH "Optional unchanged archived PMStars.exe fixture")
set(TWOINE_RUNBOX_EXE "" CACHE FILEPATH "Optional RunBox executable built from archived source")
if(TARGET lx_loader)
    if(TWOINE_PORTABLE_LOADER)
        add_test(
            NAME network_guest
            COMMAND
                "${Python3_EXECUTABLE}"
                "${TWOINE_TEST_SOURCE_DIR}/integration/os2/mptn/network_guest.py" --build
                "${PROJECT_BINARY_DIR}" --openssl "${TWOINE_OPENSSL_TOOL}"
        )
        set_tests_properties(network_guest PROPERTIES TIMEOUT 60)
        add_test(
            NAME network_diagnostics_guest
            COMMAND
                "${Python3_EXECUTABLE}"
                "${TWOINE_TEST_SOURCE_DIR}/integration/os2/mptn/network_diagnostics.py" --build
                "${PROJECT_BINARY_DIR}"
        )
        set_tests_properties(network_diagnostics_guest PROPERTIES TIMEOUT 45)
    endif()
    set(TWOINE_OS2_NETWORK_TOOLS
        ""
        CACHE PATH
        "Optional private MPTN directory with unchanged IBM bin and dll files"
    )
    if(TWOINE_OS2_NETWORK_TOOLS)
        add_test(
            NAME os2_network_tools
            COMMAND
                "${Python3_EXECUTABLE}"
                "${TWOINE_TEST_SOURCE_DIR}/integration/os2/mptn/network_diagnostics.py" --build
                "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}" --os2-tools
                "${TWOINE_OS2_NETWORK_TOOLS}"
        )
        set_tests_properties(os2_network_tools PROPERTIES TIMEOUT 75)
    endif()
    if(TARGET opengl)
        add_test(
            NAME opengl_guest
            COMMAND
                "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/opengl/opengl.py"
                --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}" --work
                "${PROJECT_BINARY_DIR}/opengl-results/guest"
        )
        set_tests_properties(opengl_guest PROPERTIES TIMEOUT 180)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_program(TWOINE_XVFB_RUN xvfb-run)
            if(TWOINE_XVFB_RUN)
                add_test(
                    NAME opengl_desktop_guest
                    COMMAND
                        "${TWOINE_XVFB_RUN}" -a "${Python3_EXECUTABLE}"
                        "${TWOINE_TEST_SOURCE_DIR}/integration/os2/opengl/opengl.py" --build
                        "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}" --video-driver
                        x11 --render-driver opengl --work
                        "${PROJECT_BINARY_DIR}/opengl-results/desktop"
                )
                add_test(
                    NAME opengl_desktop_pm
                    COMMAND "${TWOINE_XVFB_RUN}" -a $<TARGET_FILE:twoine_opengl_pm_tests>
                )
                set_tests_properties(opengl_desktop_guest opengl_desktop_pm PROPERTIES TIMEOUT 180)
                set_tests_properties(
                    opengl_desktop_pm
                    PROPERTIES
                        ENVIRONMENT
                            "SDL_VIDEO_DRIVER=x11;SDL_RENDER_DRIVER=opengl;LIBGL_ALWAYS_SOFTWARE=1"
                )
            endif()
        endif()
        set(TWOINE_OPENGL_SDK "" CACHE PATH "Optional OS/2 SDK fixture for SIMPLE.C")
        set(TWOINE_OPENGL_WATCOM "" CACHE PATH "Optional Linux-hosted Watcom fixture")
        if(TWOINE_OPENGL_SDK OR TWOINE_OPENGL_WATCOM)
            if(
                NOT TWOINE_OPENGL_SDK
                OR NOT TWOINE_OPENGL_WATCOM
                OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"
            )
                message(
                    FATAL_ERROR
                    "The SDK OpenGL fixture needs both TWOINE_OPENGL_SDK and TWOINE_OPENGL_WATCOM on Linux."
                )
            endif()
            add_test(
                NAME opengl_sdk
                COMMAND
                    "${Python3_EXECUTABLE}"
                    "${TWOINE_TEST_SOURCE_DIR}/integration/os2/opengl/opengl_sdk.py" --build
                    "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}" --sdk
                    "${TWOINE_OPENGL_SDK}" --watcom "${TWOINE_OPENGL_WATCOM}" --work
                    "${PROJECT_BINARY_DIR}/opengl-results/sdk"
            )
            set_tests_properties(opengl_sdk PROPERTIES TIMEOUT 90)
        endif()
    endif()
    add_test(
        NAME pm_dialog_apis
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/ps/dialogs.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(pm_dialog_apis PROPERTIES TIMEOUT 20)
    add_test(
        NAME legacy_file_apis
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/baseos/legacy.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(legacy_file_apis PROPERTIES TIMEOUT 40)
    add_test(
        NAME lx_forwarded_exports
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/emulator/lx_exports.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(lx_forwarded_exports PROPERTIES TIMEOUT 30)
    # Kept out of the build directory, whose lib*.so files form the runtime.
    add_test(
        NAME native_forwarded_exports
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/emulator/native_forwarders.py" --build
            "${PROJECT_BINARY_DIR}" --modules "$<TARGET_FILE_DIR:natfwd>" --backend
            "${_twoine_legacy_backend}"
    )
    set_tests_properties(native_forwarded_exports PROPERTIES TIMEOUT 30)
    add_test(
        NAME desktop_apis
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/ps/desktop.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(desktop_apis PROPERTIES TIMEOUT 40)
    add_test(
        NAME mux_wait_apis
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/baseos/mux_wait.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(mux_wait_apis PROPERTIES TIMEOUT 40)
    add_test(
        NAME queue_apis
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/baseos/queues.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(queue_apis PROPERTIES TIMEOUT 40)
    add_test(
        NAME legacy_session_apis
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/os2/baseos/legacy_sessions.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(legacy_session_apis PROPERTIES TIMEOUT 20)
    if(UNIX)
        add_test(
            NAME installer_operation_apis
            COMMAND
                "${Python3_EXECUTABLE}"
                "${TWOINE_TEST_SOURCE_DIR}/integration/os2/baseos/installer_lifecycle.py" --build
                "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
        )
        set_tests_properties(installer_operation_apis PROPERTIES TIMEOUT 35)
    endif()
    add_test(
        NAME exception_apis
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/baseos/exceptions.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(exception_apis PROPERTIES TIMEOUT 20)
    add_test(
        NAME rexx_apis
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/baseos/rexx_apis.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(rexx_apis PROPERTIES TIMEOUT 60)
    add_test(
        NAME critical_section_apis
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/os2/baseos/critical_sections.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(critical_section_apis PROPERTIES TIMEOUT 40)
    add_test(
        NAME titlebar_subclass_apis
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/ps/titlebar.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(titlebar_subclass_apis PROPERTIES TIMEOUT 20)
    add_test(
        NAME open32_apis
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/awe/open32.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(open32_apis PROPERTIES TIMEOUT 40)
    add_test(
        NAME fatal_report
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/emulator/fatal_report.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(fatal_report PROPERTIES TIMEOUT 120)
    add_test(
        NAME missing_import_exit
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/emulator/missing_import.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(missing_import_exit PROPERTIES TIMEOUT 40)
    add_test(
        NAME open32_semaphores
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/os2/awe/open32_semaphores.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(open32_semaphores PROPERTIES TIMEOUT 30)
    add_test(
        NAME clipboard_guest
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/ps/clipboard.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(clipboard_guest PROPERTIES TIMEOUT 20)
    add_test(
        NAME pm_window_apis
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/ps/windows.py"
            --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(pm_window_apis PROPERTIES TIMEOUT 40)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET pmmerge)
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
            add_test(
                NAME gui_${profile}
                COMMAND
                    "${Python3_EXECUTABLE}"
                    "${TWOINE_TEST_SOURCE_DIR}/integration/applications/gui.py" --build
                    "${PROJECT_BINARY_DIR}" --program "${TWOINE_${_twoine_gui_name}_EXE}" --backend
                    "${_twoine_gui_backend}" --profile "${profile}" --expected-exit
                    "${_twoine_gui_exit}" --work "${PROJECT_BINARY_DIR}/gui-results/${profile}"
            )
            set_tests_properties(gui_${profile} PROPERTIES TIMEOUT 30)
        endif()
    endforeach()
endif()

if(TARGET dive)
    add_test(
        NAME dive_guest_apis
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/mm/dive.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
endif()

if(TARGET doscalls)
    add_test(
        NAME guest_file_compat
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/os2/baseos/file_compat.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(guest_file_compat PROPERTIES TIMEOUT 20)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET dive)
    set(TWOINE_DOOM_EXE "" CACHE FILEPATH "Separately supplied source-built OS/2 Doom executable")
    set(TWOINE_DOOM_WAD "" CACHE FILEPATH "Separately supplied Doom shareware WAD")
    if(TWOINE_DOOM_EXE AND TWOINE_DOOM_WAD)
        add_test(
            NAME doom_gameplay
            COMMAND
                "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/ports/doom.py"
                --build "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}" --program
                "${TWOINE_DOOM_EXE}" --wad "${TWOINE_DOOM_WAD}" --source-built --work
                "${PROJECT_BINARY_DIR}/doom-results" --seconds 70
        )
        set_tests_properties(doom_gameplay PROPERTIES TIMEOUT 150)
    endif()
endif()

if(TARGET pmwinx)
    add_test(
        NAME image_install
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/ball/image_install.py"
            --build "${PROJECT_BINARY_DIR}" --work "${PROJECT_BINARY_DIR}/image-install-tests"
    )
    set_tests_properties(
        image_install
        PROPERTIES TIMEOUT 30 ENVIRONMENT "TWOINE_BALL_LIBRARY=$<TARGET_FILE:twoine_ball>"
    )
    add_test(
        NAME ball
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/ball/ball.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(ball PROPERTIES TIMEOUT 60)
endif()
if(TARGET quecalls)
    add_test(
        NAME installer_apis
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/os2/baseos/install_apis.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(installer_apis PROPERTIES TIMEOUT 60)
endif()
if(TARGET doscalls)
    add_test(
        NAME allocation_alignment
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/os2/baseos/allocation_alignment.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(allocation_alignment PROPERTIES TIMEOUT 45)
    add_test(
        NAME shared_memory_apis
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/os2/baseos/shared_memory.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(shared_memory_apis PROPERTIES TIMEOUT 60)
endif()

if(TARGET doscalls)
    add_test(
        NAME named_mutex_apis
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/os2/baseos/named_mutex.py" --build
            "${PROJECT_BINARY_DIR}" --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(named_mutex_apis PROPERTIES TIMEOUT 60)
endif()

if(TARGET lx_loader)
    add_test(
        NAME loader_startup
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/emulator/loader_startup.py" --build
            "${PROJECT_BINARY_DIR}"
    )
    set_tests_properties(loader_startup PROPERTIES TIMEOUT 10)
endif()

if(TARGET lx_loader AND TARGET twoine_ball_cli)
    add_test(
        NAME runtime_features
        COMMAND
            "${Python3_EXECUTABLE}"
            "${TWOINE_TEST_SOURCE_DIR}/integration/emulator/runtime_features.py" --build
            "${PROJECT_BINARY_DIR}" --work "${PROJECT_BINARY_DIR}/runtime-feature-tests" --backend
            "${_twoine_legacy_backend}"
    )
    set_tests_properties(runtime_features PROPERTIES TIMEOUT 40)
    add_test(
        NAME ball_relocation
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/ball/ball_relocation.py"
            --build "${PROJECT_BINARY_DIR}" --work "${PROJECT_BINARY_DIR}/ball-relocation-tests"
            --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(ball_relocation PROPERTIES TIMEOUT 40)
    if(APPLE)
        add_test(
            NAME macos_native_cache
            COMMAND
                "${Python3_EXECUTABLE}"
                "${TWOINE_TEST_SOURCE_DIR}/integration/apps/macos/macos_native_cache.py" --build
                "${PROJECT_BINARY_DIR}" --work "${PROJECT_BINARY_DIR}/macos-native-cache-tests"
        )
        set_tests_properties(macos_native_cache PROPERTIES TIMEOUT 60)
    endif()
endif()
if(TARGET lx_loader AND TARGET pmgpi)
    add_test(
        NAME bitmap_bits_guest
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/ps/bitmap_bits.py"
            --build "${PROJECT_BINARY_DIR}" --work "${PROJECT_BINARY_DIR}/bitmap-bits-tests"
            --backend "${_twoine_legacy_backend}"
    )
    set_tests_properties(bitmap_bits_guest PROPERTIES TIMEOUT 30)
endif()
