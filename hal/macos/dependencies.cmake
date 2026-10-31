# Public domain. No rights reserved. Wholly AI-authored.
# Locate SDK curses headers/libraries when no explicit include path was supplied.
if(NOT CURSES_INCLUDE_PATH)
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE TWOINE_APPLE_SDK
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    find_path(CURSES_INCLUDE_PATH curses.h HINTS "${TWOINE_APPLE_SDK}/usr/include")
    find_library(CURSES_NCURSES_LIBRARY NAMES ncursesw ncurses HINTS "${TWOINE_APPLE_SDK}/usr/lib")
endif()
