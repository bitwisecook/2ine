# Public domain. No rights reserved. Wholly AI-authored.
add_library(twoine_formats INTERFACE)
target_include_directories(twoine_formats INTERFACE "${PROJECT_SOURCE_DIR}")
target_sources(twoine_formats INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/decompress.c")
