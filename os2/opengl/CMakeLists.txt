# Derived from original 2ine; zlib-style license. See ../../LICENSE.txt.
# OpenGL (IBM src/gos/3d): OPENGL.DLL with the PGL entry points.
if(TWOINE_OPENGL)
    twoine_os2_dll(
        opengl
        opengl/module.c
        opengl/pgl.c
        opengl/memory.c
        opengl/custom.c
        opengl/arrays.c
        opengl/copy.c
    )
    target_link_libraries(opengl PRIVATE pmwin pmgpi)
    twoine_add_host_opengl(opengl)
endif()
