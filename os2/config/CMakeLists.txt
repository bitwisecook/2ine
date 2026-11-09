# Public domain. No rights reserved. Wholly AI-authored.
# Codecs for the files OS/2 keeps its configuration in: INI profiles,
# CONFIG.SYS and Workplace Shell object records. PMSHAPI and PMWP use them in
# the guest ABI; the ball's configuration editor and the installation manager
# use them in the host ABI, so each ABI gets its own static library.
function(twoine_add_os2_config target)
    set(_config "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    cmake_parse_arguments(PARSE_ARGV 1 OS2_CONFIG "I386" "" "")
    add_library(
        ${target}
        STATIC
        "${_config}/config_sys.c"
        "${_config}/os2_ini.c"
        "${_config}/wps_objects.c"
    )
    cmake_path(GET _config PARENT_PATH _os2)
    cmake_path(GET _os2 PARENT_PATH _root)
    target_include_directories(${target} PUBLIC "${_root}")
    target_compile_features(${target} PRIVATE c_std_99)
    # Linked into shared libraries that must not re-export these helpers.
    set_target_properties(
        ${target}
        PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden
    )
    if(OS2_CONFIG_I386)
        target_compile_options(${target} PRIVATE -m32)
    endif()
endfunction()
# twoine_os2_config matches the OS/2 application ABI; twoine_os2_config_host the host ABI.
if(TWOINE_NATIVE_LOADER)
    twoine_add_os2_config(twoine_os2_config I386)
    twoine_add_os2_config(twoine_os2_config_host)
else()
    twoine_add_os2_config(twoine_os2_config)
    add_library(twoine_os2_config_host ALIAS twoine_os2_config)
endif()
