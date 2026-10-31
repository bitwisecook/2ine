# Derived from original 2ine; zlib-style license. See ../../../LICENSE.txt.
# Headers and existing Debian-style i386 libraries for direct x86 execution.
include_directories("/usr/local/include/SDL2" "/usr/include/SDL2")
function(twoine_link_native_dependencies)
    target_link_libraries(viocalls /usr/lib/i386-linux-gnu/libncursesw.so.6)
    target_link_libraries(2ine "${PROJECT_SOURCE_DIR}/libSDL2-2.0.so.0")
    target_link_libraries(pmwin "${PROJECT_SOURCE_DIR}/libSDL2-2.0.so.0")
endfunction()
