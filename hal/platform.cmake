# Public domain. No rights reserved. Wholly AI-authored.
# Select exactly one host OS and reuse common POSIX implementations.
set(TWOINE_HOST_SOURCES
    "${TWOINE_HAL_SOURCE_DIR}/common/host_arch.c"
    "${PROJECT_SOURCE_DIR}/ball/disk_image.c"
    "${PROJECT_SOURCE_DIR}/ball/read_stream.c"
    "${PROJECT_SOURCE_DIR}/ball/fat_volume.c"
    "${PROJECT_SOURCE_DIR}/ball/hpfs_volume.c"
    "${PROJECT_SOURCE_DIR}/ball/vendor/fatfs/ff.c"
    "${PROJECT_SOURCE_DIR}/ball/vendor/fatfs/ffunicode.c")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(TWOINE_HOST_PLATFORM linux)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(TWOINE_HOST_PLATFORM macos)
elseif(WIN32)
    message(FATAL_ERROR "hal/win contains partial Win32 adapters; a filesystem adapter and full Windows application integration are not implemented.")
else()
    message(FATAL_ERROR "No 2ine host HAL for ${CMAKE_SYSTEM_NAME}; supported host platforms are Linux and macOS.")
endif()
foreach(service host host_child host_operation host_fs host_library host_process host_shared host_sync host_thread host_time host_socket host_network host_mailbox)
    list(APPEND TWOINE_HOST_SOURCES "${TWOINE_HAL_SOURCE_DIR}/posix/${service}.c")
endforeach()
foreach(service host_process host_sync host_system host_metadata host_file_ranges host_identity)
    list(APPEND TWOINE_HOST_SOURCES "${TWOINE_HAL_SOURCE_DIR}/${TWOINE_HOST_PLATFORM}/${service}.c")
endforeach()
