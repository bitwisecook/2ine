# Public domain. No rights reserved. Wholly AI-authored.
# 2ine-ball: the command-line client of the ball engine's JSON facade.
cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH TWOINE_CLI_APPS)
cmake_path(GET TWOINE_CLI_APPS PARENT_PATH TWOINE_CLI_PROJECT_ROOT)
add_executable(twoine_ball_cli cli.c "${TWOINE_CLI_PROJECT_ROOT}/ball/vendor/cjson/cJSON.c")
target_link_libraries(twoine_ball_cli PRIVATE twoine_ball)
target_include_directories(twoine_ball_cli PRIVATE "${TWOINE_CLI_PROJECT_ROOT}")
target_compile_features(twoine_ball_cli PRIVATE c_std_11)
target_compile_definitions(twoine_ball_cli PRIVATE CJSON_HIDE_SYMBOLS)
set_target_properties(
    twoine_ball_cli
    PROPERTIES OUTPUT_NAME "2ine-ball" RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
)
