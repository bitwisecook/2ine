# Public domain. No rights reserved. Wholly AI-authored.
if(NOT TWOINE_RUNTIME_ONLY)
    add_library(
        2ine
        SHARED
        core/bootstrap.c
        core/process.c
        core/threads.c
        core/config.c
        core/features.c
        core/drives.c
        audio.c
        ../guest/handles.c
    )
    twoine_application_target(2ine)
    target_compile_definitions(
        2ine
        PRIVATE TWOINE_VIDEO=$<BOOL:${TWOINE_VIDEO}> TWOINE_OPENGL=$<BOOL:${TWOINE_OPENGL}>
    )
    if(TWOINE_NATIVE_LOADER)
        twoine_add_host_services(twoine_native_host native)
        twoine_add_execution_core(2ine)
        target_sources(2ine PRIVATE ../guest/arguments.c)
        target_link_libraries(2ine PRIVATE twoine_native_host)
    else()
        target_link_libraries(2ine PRIVATE twoine_runtime_objects twoine_portable_host)
    endif()
    twoine_add_host_media(2ine)
    target_link_libraries(2ine PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    set_target_properties(2ine PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
endif()
