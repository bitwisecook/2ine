# Derived from original 2ine; zlib-style license. See ../../../LICENSE.txt.
# The worker needs matching i386 terminal libraries; SDL3 is built from source.
function(twoine_link_native_terminal target)
    find_library(TWOINE_I386_CURSES NAMES ncursesw
        PATHS /usr/lib/i386-linux-gnu /usr/lib32 NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(${target} PRIVATE "${TWOINE_I386_CURSES}")
endfunction()

# Font configure and compile checks run in a separate project for the worker ABI.
function(twoine_add_native_freetype)
    include(ExternalProject)
    set(_twoine_ft_prefix "${PROJECT_BINARY_DIR}/_deps/freetype-i386")
    file(MAKE_DIRECTORY "${_twoine_ft_prefix}/include/freetype2")
    if(TWOINE_FREETYPE_SOURCE_DIR)
        set(_twoine_ft_download SOURCE_DIR "${TWOINE_FREETYPE_SOURCE_DIR}")
    else()
        set(_twoine_ft_download URL "${_twoine_ft_url}" URL_HASH "${_twoine_ft_hash}")
    endif()
    ExternalProject_Add(twoine_freetype_native ${_twoine_ft_download}
        BINARY_DIR "${PROJECT_BINARY_DIR}/_deps/freetype-i386-build"
        CMAKE_ARGS -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM} -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_C_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32
            -DCMAKE_INSTALL_PREFIX=${_twoine_ft_prefix} -DCMAKE_INSTALL_LIBDIR=lib
            ${_twoine_ft_options}
        BUILD_BYPRODUCTS "${_twoine_ft_prefix}/lib/libfreetype.a")
    add_library(twoine_freetype STATIC IMPORTED GLOBAL)
    set_target_properties(twoine_freetype PROPERTIES
        IMPORTED_LOCATION "${_twoine_ft_prefix}/lib/libfreetype.a"
        INTERFACE_INCLUDE_DIRECTORIES "${_twoine_ft_prefix}/include/freetype2")
    add_dependencies(twoine_freetype twoine_freetype_native)
endfunction()
