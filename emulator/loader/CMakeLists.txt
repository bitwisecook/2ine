# Derived from original 2ine; zlib-style license. See ../../LICENSE.txt.
# The loader library: LX/NE image loading, module records, import binding and
# program start. apps/lx_loader links it into the lx_loader program.
if(LX_LEGACY)
    # Object files rather than an archive: the program references only
    # lx_loader_run, and every loader unit must still be linked.
    add_library(
        twoine_loader
        OBJECT
        run.c
        modules.c
        diagnostics.c
        entry.c
        imports.c
        load_ne.c
        load_lx.c
        rexx_service.c
    )
    twoine_application_target(twoine_loader)
    target_link_libraries(twoine_loader PUBLIC 2ine twoine_formats ${CMAKE_DL_LIBS})
    if(TWOINE_NATIVE_LOADER)
        target_compile_options(twoine_loader PRIVATE -no-pie)
    endif()
endif()
