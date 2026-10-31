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
    target_include_directories(${target} PUBLIC "${TWOINE_HAL_SOURCE_DIR}/include"
        PRIVATE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/runtime")
    target_compile_features(${target} PUBLIC c_std_99)
    target_link_libraries(${target} PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    target_sources(${target} PRIVATE ${TWOINE_HOST_SOURCES})
    if(backend STREQUAL "unicorn")
        include("${TWOINE_HAL_SOURCE_DIR}/emulator/unicorn.cmake")
        target_sources(${target} PRIVATE
            "${TWOINE_HAL_SOURCE_DIR}/emulator/cpu.c"
            "${TWOINE_HAL_SOURCE_DIR}/emulator/cpu_unicorn.c"
            "${TWOINE_HAL_SOURCE_DIR}/emulator/execution_unicorn.c")
        target_include_directories(${target} PUBLIC "${TWOINE_HAL_SOURCE_DIR}/emulator")
        target_compile_definitions(${target} PUBLIC TWOINE_WITH_UNICORN=1)
        target_link_libraries(${target} PUBLIC ${TWOINE_UNICORN_TARGET})
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
        endif()
    elseif(backend STREQUAL "native")
        if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
           NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86|x86_64|amd64|AMD64)$")
            message(FATAL_ERROR "The native HAL requires Linux x86.")
        endif()
        target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/linux/i386/execution.c")
        target_compile_definitions(${target} PUBLIC TWOINE_NATIVE_LINUX_I386=1)
        target_compile_options(${target} PRIVATE -m32)
    else()
        message(FATAL_ERROR "Unknown 2ine HAL backend: ${backend}")
    endif()
endfunction()

# These bridges must be linked in their API module, where their C entries live.
function(twoine_hal_add_native_abi target api)
    target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/linux/i386/abi_${api}.c")
endfunction()

function(twoine_add_media target)
    target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/sdl/audio.c" "${TWOINE_HAL_SOURCE_DIR}/sdl/video.c")
    target_link_libraries(${target} PRIVATE SDL3::SDL3)
endfunction()

function(twoine_add_terminal target)
    target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/posix/terminal_curses.c")
    target_include_directories(${target} PRIVATE ${CURSES_INCLUDE_DIRS})
    if(TWOINE_NATIVE_LOADER)
        include("${TWOINE_HAL_SOURCE_DIR}/linux/i386/dependencies.cmake")
        twoine_link_native_terminal(${target})
    else()
        target_link_libraries(${target} PRIVATE ${CURSES_LIBRARIES})
    endif()
endfunction()
