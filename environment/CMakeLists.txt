# Public domain. No rights reserved. Wholly AI-authored.
# The OS/2 installation manager. It works on a ball through the ball store
# (ball/store.h) and is linked into the ball engine library with it.
add_library(
    twoine_environment
    OBJECT
    audio.c
    install_records.c
    wps_sync.c
    components/operations.c
    components/search_paths.c
    components/som.c
    components/store.c
    components/system_modules.c
    dependencies/audit.c
    dependencies/database.c
    dependencies/freshness.c
    media/extract.c
    media/groups.c
    media/support.c
    media/tree.c
)
cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH TWOINE_ENVIRONMENT_PROJECT_ROOT)
target_include_directories(twoine_environment PRIVATE "${TWOINE_ENVIRONMENT_PROJECT_ROOT}")
target_link_libraries(
    twoine_environment
    PRIVATE twoine_formats_host twoine_os2_config_host twoine_storage
)
target_compile_features(twoine_environment PRIVATE c_std_11)
target_compile_definitions(twoine_environment PRIVATE CJSON_HIDE_SYMBOLS)
set_target_properties(
    twoine_environment
    PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden
)
