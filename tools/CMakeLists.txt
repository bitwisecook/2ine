# Derived from original 2ine; zlib-style license. See ../LICENSE.txt.
add_executable(lx_dump lx_dump.c)
target_link_libraries(lx_dump PRIVATE twoine_formats)
target_include_directories(lx_dump PRIVATE "${PROJECT_SOURCE_DIR}/hal/include")
set_target_properties(lx_dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
