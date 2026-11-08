# Public domain. No rights reserved.
# Wholly AI-authored.
# Execution backends: native i386 on Linux x86 hosts, Unicorn emulation elsewhere.
# Each backend joins the host object library of the worker that runs it.
set(TWOINE_EXECUTION_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(twoine_add_execution_backend target backend)
    if(backend STREQUAL "unicorn")
        include("${TWOINE_EXECUTION_SOURCE_DIR}/unicorn/unicorn.cmake")
        target_sources(
            ${target}
            PRIVATE
                "${TWOINE_EXECUTION_SOURCE_DIR}/unicorn/cpu.c"
                "${TWOINE_EXECUTION_SOURCE_DIR}/unicorn/cpu_unicorn.c"
                "${TWOINE_EXECUTION_SOURCE_DIR}/unicorn/execution.c"
        )
        target_compile_definitions(${target} PUBLIC TWOINE_WITH_UNICORN=1)
        target_link_libraries(${target} PUBLIC ${TWOINE_UNICORN_TARGET})
    elseif(backend STREQUAL "native")
        if(
            NOT CMAKE_SYSTEM_NAME STREQUAL "Linux"
            OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86|x86_64|amd64|AMD64)$"
        )
            message(FATAL_ERROR "Native i386 execution requires Linux x86.")
        endif()
        target_sources(${target} PRIVATE "${TWOINE_EXECUTION_SOURCE_DIR}/native_i386/execution.c")
        target_compile_definitions(${target} PUBLIC TWOINE_NATIVE_LINUX_I386=1)
    else()
        message(FATAL_ERROR "Unknown 2ine execution backend: ${backend}")
    endif()
endfunction()

# These bridges must be linked in their API module, where their C entries live.
function(twoine_add_native_abi target api)
    target_sources(${target} PRIVATE "${TWOINE_EXECUTION_SOURCE_DIR}/native_i386/abi_${api}.c")
endfunction()
