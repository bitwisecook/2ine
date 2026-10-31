# Public domain. No rights reserved. Wholly AI-authored.
set(TWOINE_SDL3_VERSION 3.4.18)
set(TWOINE_SDL3_SOURCE_DIR "" CACHE PATH "Local SDL 3.4.18 sources for offline builds")
set(TWOINE_SDL3_PROVIDER source CACHE STRING "SDL3 dependency provider: source or system")
set_property(CACHE TWOINE_SDL3_PROVIDER PROPERTY STRINGS source system)
set(_twoine_sdl_url https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz)
set(_twoine_sdl_hash SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
if(TWOINE_SDL3_PROVIDER STREQUAL "system")
    if(TWOINE_NATIVE_LOADER)
        # Package selection must match the worker ABI, not the optional 64-bit runtime.
        set(_twoine_pointer_size "${CMAKE_SIZEOF_VOID_P}")
        set(CMAKE_SIZEOF_VOID_P 4)
    endif()
    find_package(SDL3 ${TWOINE_SDL3_VERSION} EXACT CONFIG REQUIRED)
    if(TWOINE_NATIVE_LOADER)
        set(CMAKE_SIZEOF_VOID_P "${_twoine_pointer_size}")
    endif()
elseif(TWOINE_SDL3_PROVIDER STREQUAL "source")
    if(TWOINE_SDL3_SOURCE_DIR)
        file(READ "${TWOINE_SDL3_SOURCE_DIR}/include/SDL3/SDL_version.h" _twoine_sdl_version_header)
        foreach(component MAJOR MINOR MICRO)
            string(REGEX MATCH "#define SDL_${component}_VERSION[ \t]+([0-9]+)" _match "${_twoine_sdl_version_header}")
            set(_twoine_sdl_${component} "${CMAKE_MATCH_1}")
        endforeach()
        if(NOT "${_twoine_sdl_MAJOR}.${_twoine_sdl_MINOR}.${_twoine_sdl_MICRO}" VERSION_EQUAL TWOINE_SDL3_VERSION)
            message(FATAL_ERROR "TWOINE_SDL3_SOURCE_DIR must contain SDL ${TWOINE_SDL3_VERSION}")
        endif()
    endif()
    if(TWOINE_NATIVE_LOADER)
        # SDL configure/compile checks run in their own i386 project, while the
        # portable runtime can retain the host ABI in the surrounding project.
        include(ExternalProject)
        set(_twoine_sdl_prefix "${PROJECT_BINARY_DIR}/_deps/sdl3-i386")
        file(MAKE_DIRECTORY "${_twoine_sdl_prefix}/include")
        if(TWOINE_SDL3_SOURCE_DIR)
            set(_twoine_sdl_download SOURCE_DIR "${TWOINE_SDL3_SOURCE_DIR}")
        else()
            set(_twoine_sdl_download URL "${_twoine_sdl_url}" URL_HASH "${_twoine_sdl_hash}")
        endif()
        ExternalProject_Add(twoine_sdl3_native
            ${_twoine_sdl_download}
            BINARY_DIR "${PROJECT_BINARY_DIR}/_deps/sdl3-i386-build"
            CMAKE_ARGS -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
                -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-m32
                -DCMAKE_SHARED_LINKER_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32
                -DCMAKE_INSTALL_PREFIX=${_twoine_sdl_prefix} -DCMAKE_INSTALL_LIBDIR=lib
                -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF
                -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11_XTEST=OFF
            BUILD_BYPRODUCTS "${_twoine_sdl_prefix}/lib/libSDL3.so")
        add_library(twoine_sdl3 SHARED IMPORTED GLOBAL)
        set_target_properties(twoine_sdl3 PROPERTIES
            IMPORTED_LOCATION "${_twoine_sdl_prefix}/lib/libSDL3.so"
            INTERFACE_INCLUDE_DIRECTORIES "${_twoine_sdl_prefix}/include")
        add_dependencies(twoine_sdl3 twoine_sdl3_native)
        add_library(SDL3::SDL3 ALIAS twoine_sdl3)
    else()
        set(SDL_SHARED ON CACHE BOOL "SDL shared library" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "SDL static library" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "SDL test programs" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "SDL test support library" FORCE)
        set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "Allow missing optional desktop development packages")
        set(SDL_X11_XTEST OFF CACHE BOOL "XTest input injection is not used by 2ine")
        if(TWOINE_SDL3_SOURCE_DIR)
            add_subdirectory("${TWOINE_SDL3_SOURCE_DIR}" "${PROJECT_BINARY_DIR}/_deps/sdl3-build" EXCLUDE_FROM_ALL)
        else()
            include(FetchContent)
            FetchContent_Declare(SDL3 URL "${_twoine_sdl_url}" URL_HASH "${_twoine_sdl_hash}")
            FetchContent_MakeAvailable(SDL3)
        endif()
    endif()
else()
    message(FATAL_ERROR "Unknown TWOINE_SDL3_PROVIDER: ${TWOINE_SDL3_PROVIDER}")
endif()
