# Public domain. No rights reserved.
# Wholly AI-authored.
# Each worker has its own HAL objects so native i386 and portable host objects
# can be built together without sharing compiler flags or backend definitions.
set(TWOINE_HAL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)

function(twoine_add_hal target backend)
    if(NOT UNIX)
        message(FATAL_ERROR "The complete host service set currently builds on Linux/macOS; Windows has no filesystem adapter.")
    endif()
    find_package(Threads REQUIRED)
    add_library(${target} OBJECT)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(${target} PUBLIC "${TWOINE_HAL_SOURCE_DIR}/../runtime")
    target_compile_features(${target} PUBLIC c_std_99)
    target_link_libraries(${target} PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    foreach(service host host_process host_sync host_system host_shared host_child host_library host_thread host_fs_posix)
        target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/host/${service}.c")
    endforeach()
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
        target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/native/execution_linux_i386.c")
        target_compile_definitions(${target} PUBLIC TWOINE_NATIVE_LINUX_I386=1)
        target_compile_options(${target} PRIVATE -m32)
    else()
        message(FATAL_ERROR "Unknown 2ine HAL backend: ${backend}")
    endif()
endfunction()

# These bridges must be linked in their API module, where their C entries live.
function(twoine_hal_add_native_abi target api)
    target_sources(${target} PRIVATE "${TWOINE_HAL_SOURCE_DIR}/native/abi_linux_i386_${api}.c")
endfunction()
