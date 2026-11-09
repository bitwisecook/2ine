# Public domain. No rights reserved. Wholly AI-authored.
# Launch planning for a ball: runtime selection, the guest and host
# environments, the 2ine.cfg and audio.conf projections and the launch plan.
# It reads the ball through the ball store (ball/store.h) and the installation
# manager (environment/), and is linked into the ball engine library with them.
add_library(
    twoine_launch
    OBJECT
    argv.c
    audio_config.c
    dependencies.c
    desktop_identity.c
    features.c
    host_environment.c
    interpreter.c
    plan.c
    prerequisites.c
    runtime.c
    runtime_config.c
    runtime_selection.c
    settings_schema.c
)
cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH TWOINE_LAUNCH_EMULATOR)
cmake_path(GET TWOINE_LAUNCH_EMULATOR PARENT_PATH TWOINE_LAUNCH_PROJECT_ROOT)
target_include_directories(twoine_launch PRIVATE "${TWOINE_LAUNCH_PROJECT_ROOT}")
target_link_libraries(twoine_launch PRIVATE twoine_formats_host twoine_storage)
target_compile_features(twoine_launch PRIVATE c_std_11)
target_compile_definitions(twoine_launch PRIVATE CJSON_HIDE_SYMBOLS)
set_target_properties(
    twoine_launch
    PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden
)
