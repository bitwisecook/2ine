# Public domain. No rights reserved. Wholly AI-authored.
# twoine_runtime: execution dispatch, guest memory and the universal thunk over the
# portable (Unicorn) backend. The native lib2ine compiles the same sources itself
# with its i386 flags through twoine_add_execution_core().
if(TWOINE_RUNTIME_ONLY OR TWOINE_BUILD_RUNTIME OR TWOINE_PORTABLE_LOADER OR NOT LX_LEGACY)
    twoine_add_host_services(twoine_portable_host unicorn)
    add_library(twoine_runtime_objects OBJECT)
    twoine_add_abi_arguments(twoine_runtime_objects)
    twoine_add_execution_core(twoine_runtime_objects)
    set_target_properties(twoine_runtime_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(
        twoine_runtime_objects
        PUBLIC "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/emulator/include"
    )
    target_compile_features(twoine_runtime_objects PUBLIC c_std_99)
    target_link_libraries(twoine_runtime_objects PUBLIC twoine_portable_host)
    target_compile_options(twoine_runtime_objects PRIVATE -Wall -Wextra -Werror)
    add_library(twoine_runtime STATIC $<TARGET_OBJECTS:twoine_runtime_objects>)
    target_include_directories(
        twoine_runtime
        PUBLIC "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/emulator/include"
    )
    target_compile_features(twoine_runtime PUBLIC c_std_99)
    target_link_libraries(twoine_runtime PUBLIC twoine_portable_host)
endif()
