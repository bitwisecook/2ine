# Derived from original 2ine; zlib-style license. See ../LICENSE.txt.
add_executable(lx_dump binary/lx_dump.c)
target_link_libraries(lx_dump PRIVATE twoine_formats_host)
target_include_directories(
    lx_dump
    PRIVATE "${PROJECT_SOURCE_DIR}/host/include" "${PROJECT_SOURCE_DIR}/emulator/include"
)
set_target_properties(lx_dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
