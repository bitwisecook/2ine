# Public domain. No rights reserved. Wholly AI-authored.
# Test executables stay in the tests build directory, where the integration
# scripts look for them, whichever subdirectory defines them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# Scripts and fixtures are named from the test tree's root.
set(TWOINE_TEST_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
# The execution backend that guest-running tests select.
if(TWOINE_PORTABLE_LOADER)
    set(_twoine_legacy_backend unicorn)
else()
    set(_twoine_legacy_backend native)
endif()
add_test(
    NAME source_boundaries
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/architecture/check_boundaries.py"
)
add_subdirectory(storage)
add_subdirectory(host)
add_subdirectory(emulator)
add_subdirectory(os2)
add_subdirectory(integration)
