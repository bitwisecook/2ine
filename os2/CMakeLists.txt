# Derived from original 2ine; zlib-style license. See ../LICENSE.txt.
# One shared library per OS/2 DLL, named after the DLL module: libdoscalls.so
# provides DOSCALLS. The groups follow IBM's component build trees (README.md).

# Adds the shared library for one OS/2 DLL; sources follow the name.
function(twoine_os2_dll name)
    add_library(${name} SHARED ${ARGN})
    twoine_application_target(${name})
    target_link_libraries(${name} PRIVATE 2ine)
    set_target_properties(${name} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    if(APPLE)
        set_target_properties(${name} PROPERTIES SUFFIX ".so")
    endif()
endfunction()

add_subdirectory(baseos)
add_subdirectory(ps)
add_subdirectory(wpsh)
add_subdirectory(som)
add_subdirectory(registry)
add_subdirectory(mm)
add_subdirectory(mptn)
add_subdirectory(opengl)
add_subdirectory(thirdparty)
