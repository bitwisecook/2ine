# Public domain. No rights reserved. Wholly AI-authored.
# lib2ine: the OS/2 process runtime shared by the loader and every OS/2 library.
add_library(
    2ine
    SHARED
    bootstrap.c
    pib.c
    peers.c
    threads.c
    segments.c
    config.c
    features.c
    drives.c
    paths.c
    resolver_files.c
    audio_mixer.c
    launch_record.c
)
twoine_add_abi_handles(2ine)
twoine_application_target(2ine)
target_compile_definitions(
    2ine
    PRIVATE TWOINE_VIDEO=$<BOOL:${TWOINE_VIDEO}> TWOINE_OPENGL=$<BOOL:${TWOINE_OPENGL}>
)
if(TWOINE_NATIVE_LOADER)
    twoine_add_host_services(twoine_native_host native)
    twoine_add_execution_core(2ine)
    twoine_add_abi_arguments(2ine)
    target_link_libraries(2ine PRIVATE twoine_native_host)
else()
    target_link_libraries(2ine PRIVATE twoine_runtime_objects twoine_portable_host)
endif()
twoine_add_host_media(2ine)
target_link_libraries(2ine PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(2ine PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
