# Host services shared by the native worker and the portable execution library.
if(NOT UNIX)
    message(FATAL_ERROR "The complete host service set currently builds on Linux/macOS; Windows has no filesystem adapter.")
endif()
set(TWOINE_HOST_SOURCES)
foreach(service host host_process host_sync host_system host_shared host_child host_library host_thread host_fs_posix)
    list(APPEND TWOINE_HOST_SOURCES "${CMAKE_CURRENT_LIST_DIR}/${service}.c")
endforeach()
