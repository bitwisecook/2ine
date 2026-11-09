# Public domain. No rights reserved. Wholly AI-authored.
# OPENGL: PGL presentation through PM.
if(TARGET opengl)
    add_executable(twoine_opengl_pm_tests test_opengl.c)
    twoine_application_target(twoine_opengl_pm_tests)
    target_link_libraries(twoine_opengl_pm_tests PRIVATE opengl pmmerge pmgpi 2ine)
    add_test(NAME opengl_pm COMMAND twoine_opengl_pm_tests)
    set_tests_properties(
        opengl_pm
        PROPERTIES TIMEOUT 60 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_property(TEST opengl_pm APPEND PROPERTY ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
    endif()
endif()
