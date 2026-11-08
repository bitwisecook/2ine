# Public domain. No rights reserved.
# Wholly AI-authored.
# Each worker has its own HAL objects so native i386 and portable host objects
# can be built together without sharing compiler flags or backend definitions.
set(TWOINE_HAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)

function(twoine_add_hal target backend)
    include("${TWOINE_HAL_SOURCE_DIR}/platform.cmake")
    find_package(Threads REQUIRED)
    add_library(${target} OBJECT)
    set_property(TARGET ${target} PROPERTY TWOINE_HOST_PLATFORM "${TWOINE_HOST_PLATFORM}")
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(
        ${target}
        PUBLIC "${TWOINE_HAL_SOURCE_DIR}/include"
        PRIVATE "${PROJECT_SOURCE_DIR}"
    )
    target_compile_features(${target} PUBLIC c_std_99)
    target_link_libraries(${target} PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    find_package(OpenSSL 4.1 REQUIRED COMPONENTS SSL Crypto)
    target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/posix/host_tls.c")
    if(backend STREQUAL "native")
        # FindOpenSSL runs under the host ABI; the native worker uses i386.
        target_include_directories(${target} PRIVATE ${OPENSSL_INCLUDE_DIR})
        foreach(library ssl crypto)
            execute_process(
                COMMAND "${CMAKE_C_COMPILER}" -m32 -print-file-name=lib${library}.so
                OUTPUT_VARIABLE _twoine_tls_${library}
                OUTPUT_STRIP_TRAILING_WHITESPACE
                COMMAND_ERROR_IS_FATAL ANY
            )
            if(
                NOT IS_ABSOLUTE "${_twoine_tls_${library}}"
                OR NOT EXISTS "${_twoine_tls_${library}}"
            )
                message(
                    FATAL_ERROR
                    "Install the i386 OpenSSL 4.1 development libraries for the native worker"
                )
            endif()
            target_link_libraries(${target} PUBLIC "${_twoine_tls_${library}}")
        endforeach()
    else()
        target_link_libraries(${target} PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    endif()
    if(APPLE)
        target_link_libraries(
            ${target}
            PUBLIC "-framework SystemConfiguration" "-framework CoreFoundation"
        )
    endif()
    target_sources(${target} PRIVATE ${TWOINE_HOST_SOURCES})
    # Mapped disk-image drives; each backend builds storage for its own ABI.
    set(_storage_options VOLUMES_ONLY)
    if(backend STREQUAL "native")
        list(APPEND _storage_options I386)
    elseif(backend STREQUAL "unicorn")
        list(APPEND _storage_options STRICT)
    endif()
    twoine_add_storage(${target}_storage ${_storage_options})
    target_link_libraries(${target} PUBLIC ${target}_storage)
    file(STRINGS "${PROJECT_SOURCE_DIR}/VERSION" _twoine_version LIMIT_COUNT 1)
    set_property(
        SOURCE "${TWOINE_HAL_SOURCE_DIR}/posix/host_fatal.c" TARGET_DIRECTORY ${target}
        APPEND
        PROPERTY COMPILE_DEFINITIONS TWOINE_VERSION_TEXT="${_twoine_version}"
    )
    twoine_add_execution_backend(${target} ${backend})
    if(backend STREQUAL "unicorn")
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
        endif()
    elseif(backend STREQUAL "native")
        target_compile_options(${target} PRIVATE -m32)
    endif()
endfunction()

function(twoine_add_media target)
    if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/posix/print_spool.c")
    endif()
    target_sources(
        ${target}
        PRIVATE
            "${TWOINE_HAL_SOURCE_DIR}/common/fatal_dialog.c"
            "${TWOINE_HAL_SOURCE_DIR}/sdl/audio.c"
            "${TWOINE_HAL_SOURCE_DIR}/sdl/video.c"
            "${TWOINE_HAL_SOURCE_DIR}/sdl/clipboard.c"
            "${TWOINE_HAL_SOURCE_DIR}/sdl/clipboard_codec.c"
    )
    if(WIN32)
        target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/win/clipboard_formats.c")
    else()
        target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/sdl/clipboard_native_none.c")
    endif()
    target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/common/font.c")
    target_compile_definitions(${target} PRIVATE TWOINE_FONT_DIRECTORY="${TWOINE_RUNTIME_FONT_DIR}")
    target_link_libraries(${target} PRIVATE SDL3::SDL3 twoine_freetype)
    if(APPLE)
        target_sources(
            ${target}
            PRIVATE
                "${TWOINE_HAL_SOURCE_DIR}/macos/fatal_dialog.m"
                "${TWOINE_HAL_SOURCE_DIR}/macos/window_chrome.m"
                "${TWOINE_HAL_SOURCE_DIR}/macos/print_dialog.m"
                "${TWOINE_HAL_SOURCE_DIR}/macos/desktop_session.m"
                "${TWOINE_HAL_SOURCE_DIR}/macos/desktop_bundle.m"
                "${TWOINE_HAL_SOURCE_DIR}/posix/desktop_lease.c"
                "${TWOINE_HAL_SOURCE_DIR}/common/desktop_group.c"
                "${TWOINE_HAL_SOURCE_DIR}/common/desktop_cycle.c"
                "${TWOINE_HAL_SOURCE_DIR}/posix/desktop_cycle.c"
        )
        target_link_libraries(${target} PRIVATE twoine_formats)
        target_include_directories(${target} PRIVATE "${PROJECT_BINARY_DIR}/generated")
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>")
        target_link_libraries(
            ${target}
            PRIVATE "-framework AppKit" "-framework UniformTypeIdentifiers" "-framework ImageIO"
        )
    else()
        target_sources(
            ${target}
            PRIVATE
                "${TWOINE_HAL_SOURCE_DIR}/sdl/fatal_dialog.c"
                "${TWOINE_HAL_SOURCE_DIR}/sdl/window_chrome_none.c"
                "${TWOINE_HAL_SOURCE_DIR}/sdl/desktop_session_none.c"
                "${TWOINE_HAL_SOURCE_DIR}/sdl/desktop_bundle_none.c"
        )
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/linux/print_dialog.c")
        elseif(WIN32)
            target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/win/print_dialog.c")
            target_link_libraries(${target} PRIVATE comdlg32 winspool comctl32)
        endif()
    endif()
endfunction()

function(twoine_add_terminal target)
    target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/posix/terminal_curses.c")
    target_include_directories(${target} PRIVATE ${CURSES_INCLUDE_DIRS})
    if(TWOINE_NATIVE_LOADER)
        include("${TWOINE_EXECUTION_SOURCE_DIR}/native_i386/dependencies.cmake")
        twoine_link_native_terminal(${target})
    else()
        target_link_libraries(${target} PRIVATE ${CURSES_LIBRARIES})
    endif()
endfunction()

function(twoine_add_opengl target)
    target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/mesa/opengl.c")
    if(APPLE)
        target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/macos/gl_policy.c")
        target_compile_definitions(
            ${target}
            PRIVATE TWOINE_KOSMICKRISP_ICD="${TWOINE_KOSMICKRISP_ICD}"
        )
    else()
        target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/linux/gl_policy.c")
    endif()
    target_include_directories(${target} PRIVATE "${TWOINE_EGL_INCLUDE_DIR}")
    if(TWOINE_MESA_ROOT AND NOT APPLE)
        target_link_directories(${target} PRIVATE "${TWOINE_MESA_ROOT}/lib")
    endif()
    target_link_libraries(${target} PRIVATE ${TWOINE_EGL_LIBRARY})
endfunction()

# The fallback dock icon for command-line guests: the 2ine ball of twine,
# embedded at configure time so a loader copied anywhere can present it.
if(APPLE AND NOT TWOINE_RUNTIME_ONLY)
    file(
        READ "${PROJECT_SOURCE_DIR}/macos/Twoine.iconset/icon_256x256.png"
        _twoine_ball_icon_hex
        HEX
    )
    string(LENGTH "${_twoine_ball_icon_hex}" _twoine_ball_icon_length)
    math(EXPR _twoine_ball_icon_size "${_twoine_ball_icon_length} / 2")
    file(
        WRITE "${PROJECT_BINARY_DIR}/generated/twoine_ball_icon_png.h"
        "/* Generated from macos/Twoine.iconset/icon_256x256.png. Do not edit. */\n"
        "#define TWOINE_BALL_ICON_PNG_SIZE ${_twoine_ball_icon_size}\n"
        "#define TWOINE_BALL_ICON_PNG_HEX \"${_twoine_ball_icon_hex}\"\n"
    )
    unset(_twoine_ball_icon_hex)
    unset(_twoine_ball_icon_length)
    unset(_twoine_ball_icon_size)
endif()
