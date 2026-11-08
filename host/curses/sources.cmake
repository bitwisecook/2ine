# Public domain. No rights reserved. Wholly AI-authored.
# curses character-cell terminal. host/CMakeLists.txt links the curses build
# that matches each worker's ABI.
twoine_host_function(terminal terminal/terminal.c)
twoine_host_requires(terminal INCLUDE ${CURSES_INCLUDE_DIRS})
