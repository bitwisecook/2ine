# Derived from original 2ine; zlib-style license. See ../../../LICENSE.txt.
# The worker needs matching i386 terminal libraries; SDL3 is built from source.
function(twoine_link_native_terminal target)
    find_library(TWOINE_I386_CURSES NAMES ncursesw
        PATHS /usr/lib/i386-linux-gnu /usr/lib32 NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(${target} PRIVATE "${TWOINE_I386_CURSES}")
endfunction()
