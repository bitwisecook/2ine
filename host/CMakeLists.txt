# Public domain. No rights reserved.
# Wholly AI-authored.
# Host services: public contracts in include/host/, platform-free code in core/
# and one directory per adapter (posix, linux, macos, win, sdl, openssl,
# freetype, curses, mesa, none). Each adapter's sources.cmake lists its files
# by function; platform.cmake chooses the adapters for every function; the
# twoine_add_host_* functions below link a group of functions into a target.
#
# Each worker has its own host objects so native i386 and portable host objects
# can be built together without sharing compiler flags or backend definitions.
set(TWOINE_HOST_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)

# Called from an adapter's sources.cmake: records files, relative to the
# adapter directory, as part of one function.
function(twoine_host_function function)
    foreach(file ${ARGN})
        set_property(
            GLOBAL
            APPEND
            PROPERTY
                TWOINE_HOST_${TWOINE_HOST_ADAPTER}_${function}_SOURCES
                    "${TWOINE_HOST_SOURCE_DIR}/${TWOINE_HOST_ADAPTER}/${file}"
        )
    endforeach()
endfunction()

# Called from an adapter's sources.cmake: records what one function needs from
# the target that links it. DEFINE applies to the function's own sources;
# LINK, INCLUDE, OPTION and LINK_DIRECTORY apply to the target.
function(twoine_host_requires function kind)
    set_property(
        GLOBAL
        APPEND
        PROPERTY TWOINE_HOST_${TWOINE_HOST_ADAPTER}_${function}_${kind} ${ARGN}
    )
endfunction()

# Reads the sources.cmake of every adapter the platform selects, once. Adapter
# files read configuration variables, so this runs at the first link request.
macro(twoine_host_load)
    include("${TWOINE_HOST_SOURCE_DIR}/platform.cmake")
    get_property(_twoine_host_loaded GLOBAL PROPERTY TWOINE_HOST_LOADED)
    if(NOT _twoine_host_loaded)
        set(_twoine_host_adapters)
        foreach(_twoine_function ${TWOINE_HOST_FUNCTIONS})
            list(APPEND _twoine_host_adapters ${TWOINE_HOST_SELECT_${_twoine_function}})
        endforeach()
        list(REMOVE_DUPLICATES _twoine_host_adapters)
        foreach(TWOINE_HOST_ADAPTER ${_twoine_host_adapters})
            include("${TWOINE_HOST_SOURCE_DIR}/${TWOINE_HOST_ADAPTER}/sources.cmake")
        endforeach()
        set_property(GLOBAL PROPERTY TWOINE_HOST_LOADED TRUE)
    endif()
endmacro()

# Adds each function's sources and requirements from the selected adapters.
# visibility applies to libraries: PUBLIC for the worker objects, whose users
# link the result, and PRIVATE for modules.
function(twoine_add_host_functions target visibility)
    twoine_host_load()
    foreach(function ${ARGN})
        if(NOT DEFINED TWOINE_HOST_SELECT_${function})
            message(FATAL_ERROR "No host adapter is selected for ${function}")
        endif()
        foreach(adapter ${TWOINE_HOST_SELECT_${function}})
            set(_prefix TWOINE_HOST_${adapter}_${function})
            foreach(
                kind
                SOURCES
                DEFINE
                LINK
                INCLUDE
                OPTION
                LINK_DIRECTORY
            )
                get_property(_${kind} GLOBAL PROPERTY ${_prefix}_${kind})
            endforeach()
            if(_SOURCES)
                target_sources(${target} PRIVATE ${_SOURCES})
                if(_DEFINE)
                    set_property(
                        SOURCE ${_SOURCES} TARGET_DIRECTORY ${target}
                        APPEND
                        PROPERTY COMPILE_DEFINITIONS ${_DEFINE}
                    )
                endif()
            endif()
            if(_LINK)
                target_link_libraries(${target} ${visibility} ${_LINK})
            endif()
            if(_INCLUDE)
                target_include_directories(${target} PRIVATE ${_INCLUDE})
            endif()
            if(_OPTION)
                target_compile_options(${target} PRIVATE ${_OPTION})
            endif()
            if(_LINK_DIRECTORY)
                target_link_directories(${target} PRIVATE ${_LINK_DIRECTORY})
            endif()
        endforeach()
    endforeach()
endfunction()

# The host services every worker links: system, processes, threads, IPC,
# files, networking and fatal reporting, plus the execution backend.
function(twoine_add_host_services target backend)
    twoine_host_load()
    find_package(Threads REQUIRED)
    add_library(${target} OBJECT)
    set_property(TARGET ${target} PROPERTY TWOINE_HOST_PLATFORM "${TWOINE_HOST_PLATFORM}")
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(
        ${target}
        PUBLIC "${TWOINE_HOST_SOURCE_DIR}/include"
        PRIVATE "${PROJECT_SOURCE_DIR}"
    )
    target_compile_features(${target} PUBLIC c_std_99)
    target_link_libraries(${target} PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
    twoine_add_host_functions(
        ${target}
        PUBLIC
        system
        process
        thread
        ipc
        fs
        net
        fatal
    )
    find_package(OpenSSL 4.1 REQUIRED COMPONENTS SSL Crypto)
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
    # Mapped disk-image drives; each backend builds storage for its own ABI.
    set(_storage_options VOLUMES_ONLY)
    if(backend STREQUAL "native")
        list(APPEND _storage_options I386)
    elseif(backend STREQUAL "unicorn")
        list(APPEND _storage_options STRICT)
    endif()
    twoine_add_storage(${target}_storage ${_storage_options})
    target_link_libraries(${target} PUBLIC ${target}_storage)
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

# Windows, input, desktop integration, audio, clipboard, printing, fonts, the
# fatal-report dialog and the native dialog helper they use, linked into lib2ine.
function(twoine_add_host_media target)
    twoine_add_host_functions(
        ${target}
        PRIVATE
        fatal_dialog
        dialog
        window
        input
        desktop
        audio
        clipboard
        print
        font
    )
endfunction()

function(twoine_add_host_terminal target)
    twoine_add_host_functions(${target} PRIVATE terminal)
    if(TWOINE_NATIVE_LOADER)
        include("${TWOINE_EXECUTION_SOURCE_DIR}/native_i386/dependencies.cmake")
        twoine_link_native_terminal(${target})
    else()
        target_link_libraries(${target} PRIVATE ${CURSES_LIBRARIES})
    endif()
endfunction()

function(twoine_add_host_opengl target)
    twoine_add_host_functions(${target} PRIVATE opengl)
endfunction()
