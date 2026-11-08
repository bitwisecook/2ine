# Derived from original 2ine; zlib-style license. See ../../LICENSE.txt.
if(LX_LEGACY)
    add_executable(
        lx_loader
        main.c
        modules.c
        entry.c
        imports.c
        load_ne.c
        load_lx.c
        paths.c
        rexx_service.c
    )
    target_sources(lx_loader PRIVATE network_config.c)
    twoine_application_target(lx_loader)
    target_link_libraries(lx_loader PRIVATE 2ine twoine_formats ${CMAKE_DL_LIBS})
    set_target_properties(lx_loader PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    if(TWOINE_NATIVE_LOADER)
        set(_twoine_worker_backend "native")
    else()
        set(_twoine_worker_backend "unicorn")
    endif()
    set(_twoine_video_json false)
    set(_twoine_gl_json false)
    if(TWOINE_VIDEO)
        set(_twoine_video_json true)
    endif()
    if(TWOINE_OPENGL)
        set(_twoine_gl_json true)
    endif()
    set(_twoine_operation_json false)
    if(UNIX)
        set(_twoine_operation_json true)
    endif()
    file(
        GENERATE OUTPUT "${PROJECT_BINARY_DIR}/runtime-capabilities.json"
        CONTENT
            "{\n  \"schema\": 1,\n  \"operationSupervisor\": ${_twoine_operation_json},\n  \"backends\": [\"${_twoine_worker_backend}\"],\n  \"defaultBackend\": \"${_twoine_worker_backend}\",\n  \"defaultOpenGL\": \"mesa\",\n  \"features\": {\"mesa\": ${_twoine_gl_json}, \"video\": ${_twoine_video_json}, \"pcm\": true, \"midi\": true, \"dive\": true}\n}\n"
    )
    if(TWOINE_NATIVE_LOADER)
        target_compile_options(lx_loader PRIVATE -no-pie)
        target_link_options(lx_loader PRIVATE -no-pie)
    endif()
endif()
