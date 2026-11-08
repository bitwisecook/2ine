# Public domain. No rights reserved. Wholly AI-authored.
# Selects the adapters that implement each host function on this host OS.
# core holds platform-free code shared by every port; posix is shared by
# Linux and macOS; none supplies stubs where a port has no native integration.
set(TWOINE_HOST_FUNCTIONS
    system
    process
    thread
    ipc
    fs
    net
    fatal
    fatal_dialog
    window
    input
    desktop
    audio
    clipboard
    print
    terminal
    font
    opengl
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(TWOINE_HOST_PLATFORM linux)
    set(TWOINE_HOST_SELECT_desktop none)
    set(TWOINE_HOST_SELECT_window sdl none)
    set(TWOINE_HOST_SELECT_fatal_dialog core sdl)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(TWOINE_HOST_PLATFORM macos)
    set(TWOINE_HOST_SELECT_desktop core posix macos)
    set(TWOINE_HOST_SELECT_window sdl macos)
    set(TWOINE_HOST_SELECT_fatal_dialog core macos)
elseif(WIN32)
    # The Windows adapters by function. fs, net and fatal have none, so the
    # configuration stops.
    set(TWOINE_HOST_PLATFORM win)
    set(TWOINE_HOST_SELECT_system core win)
    set(TWOINE_HOST_SELECT_process win none)
    set(TWOINE_HOST_SELECT_thread core win)
    set(TWOINE_HOST_SELECT_ipc win)
    set(TWOINE_HOST_SELECT_desktop none)
    set(TWOINE_HOST_SELECT_window sdl none)
    set(TWOINE_HOST_SELECT_fatal_dialog core sdl)
    set(TWOINE_HOST_SELECT_clipboard core sdl win)
    set(TWOINE_HOST_SELECT_print core win)
    message(
        FATAL_ERROR
        "host/win contains partial Win32 adapters; there is no Windows filesystem, network or fatal-report adapter and no full Windows application integration."
    )
else()
    message(
        FATAL_ERROR
        "No 2ine host adapters for ${CMAKE_SYSTEM_NAME}; supported host platforms are Linux and macOS."
    )
endif()
# Functions whose adapter choice is the same on Linux and macOS.
set(TWOINE_HOST_SELECT_system core posix ${TWOINE_HOST_PLATFORM})
set(TWOINE_HOST_SELECT_process posix ${TWOINE_HOST_PLATFORM})
set(TWOINE_HOST_SELECT_thread core posix ${TWOINE_HOST_PLATFORM})
set(TWOINE_HOST_SELECT_ipc posix)
set(TWOINE_HOST_SELECT_fs posix ${TWOINE_HOST_PLATFORM})
set(TWOINE_HOST_SELECT_net posix ${TWOINE_HOST_PLATFORM} openssl)
set(TWOINE_HOST_SELECT_fatal posix ${TWOINE_HOST_PLATFORM})
set(TWOINE_HOST_SELECT_input sdl)
set(TWOINE_HOST_SELECT_audio sdl)
set(TWOINE_HOST_SELECT_clipboard core sdl none)
set(TWOINE_HOST_SELECT_print core posix ${TWOINE_HOST_PLATFORM})
set(TWOINE_HOST_SELECT_terminal curses)
set(TWOINE_HOST_SELECT_font freetype)
set(TWOINE_HOST_SELECT_opengl mesa ${TWOINE_HOST_PLATFORM})
