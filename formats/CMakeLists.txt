# Public domain. No rights reserved. Wholly AI-authored.
# Platform-free binary formats. The native i386 worker and the host-ABI ball
# engine cannot share objects, so each ABI gets its own static library.
function(twoine_add_formats target)
    set(_formats "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    cmake_parse_arguments(PARSE_ARGV 1 FORMATS "I386" "" "")
    add_library(${target} STATIC "${_formats}/executable/lx_exepack.c")
    get_filename_component(_root "${_formats}" DIRECTORY)
    target_include_directories(${target} PUBLIC "${_root}")
    target_compile_features(${target} PRIVATE c_std_99)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(FORMATS_I386)
        target_compile_options(${target} PRIVATE -m32)
    endif()
endfunction()
# twoine_formats matches the OS/2 application ABI; twoine_formats_host the host ABI.
if(TWOINE_NATIVE_LOADER)
    twoine_add_formats(twoine_formats I386)
    twoine_add_formats(twoine_formats_host)
else()
    twoine_add_formats(twoine_formats)
    add_library(twoine_formats_host ALIAS twoine_formats)
endif()
