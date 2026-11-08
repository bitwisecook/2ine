# Public domain. No rights reserved. Wholly AI-authored.
# Disk images, filesystems and archive containers, shared by the HAL and ball.
# Volume access needs only the vendored FatFs; archive containers add
# libarchive and zlib. The native i386 HAL and host-ABI ball cannot share
# objects, so each caller builds the static library for its ABI and options.
cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH _twoine_storage_root)
# macOS provides libarchive without public SDK headers; use the pinned API headers.
set(LibArchive_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libarchive")
find_package(LibArchive REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

# Options:
#   VOLUMES_ONLY       disk images and filesystems, without archive containers
#   I386               compile for the native i386 worker
#   CACHED_FILE_TIMES  request-scoped local-time cache for archive timestamps
#   HIDDEN             do not export the API from the shared object linking it
#   STRICT             treat warnings as errors
function(twoine_add_storage target)
    cmake_parse_arguments(
        PARSE_ARGV 1
        STORAGE
        "VOLUMES_ONLY;I386;CACHED_FILE_TIMES;HIDDEN;STRICT"
        ""
        ""
    )
    set(_storage "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    cmake_path(GET _storage PARENT_PATH _root)
    add_library(
        ${target}
        STATIC
        "${_storage}/common/host_source.c"
        "${_storage}/common/host_sync.c"
        "${_storage}/common/host_time.c"
        "${_storage}/common/read_stream.c"
        "${_storage}/paths/image_path.c"
        "${_storage}/volume/volume.c"
        "${_storage}/volume/volume_disk.c"
        "${_storage}/volume/directory_index.c"
        "${_storage}/volume/image_reader.c"
        "${_storage}/filesystems/iso9660/iso9660.c"
        "${_storage}/filesystems/fat/fat12_16.c"
        "${_storage}/filesystems/fat/fat_fatfs.c"
        "${_storage}/filesystems/hpfs/io.c"
        "${_storage}/filesystems/hpfs/alloc.c"
        "${_storage}/filesystems/hpfs/directory.c"
        "${_storage}/filesystems/hpfs/file.c"
        "${_storage}/filesystems/hpfs/operations.c"
        "${_storage}/filesystems/hpfs/format.c"
        "${_storage}/filesystems/hpfs/ea.c"
        "${_storage}/images/floppy.c"
        "${_storage}/vendor/fatfs/ff.c"
        "${_storage}/vendor/fatfs/ffunicode.c"
    )
    if(NOT STORAGE_VOLUMES_ONLY)
        target_sources(
            ${target}
            PRIVATE
                "${_storage}/archives/container.c"
                "${_storage}/archives/ibm_pack/decode.c"
                "${_storage}/vendor/hwzip/implode.c"
                "${_storage}/vendor/hwzip/huffman.c"
                "${_storage}/vendor/hwzip/lz77.c"
                "${_storage}/vendor/hwzip/tables.c"
                "${_storage}/vendor/hwzip/shrink.c"
                "${_storage}/vendor/hwzip/reduce.c"
        )
        target_link_libraries(${target} PUBLIC LibArchive::LibArchive ZLIB::ZLIB)
    endif()
    target_include_directories(${target} PUBLIC "${_storage}/include" PRIVATE "${_root}")
    target_compile_features(${target} PRIVATE c_std_11)
    target_link_libraries(${target} PUBLIC Threads::Threads)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(STORAGE_HIDDEN)
        set_target_properties(${target} PROPERTIES C_VISIBILITY_PRESET hidden)
    endif()
    if(STORAGE_CACHED_FILE_TIMES)
        target_compile_definitions(${target} PRIVATE TB_MEDIA_TIME_CACHE)
        if(APPLE)
            target_link_libraries(${target} PUBLIC "-framework CoreFoundation")
        endif()
    endif()
    if(STORAGE_I386)
        target_compile_options(${target} PRIVATE -m32)
    endif()
    if(STORAGE_STRICT)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endfunction()

# The ball engine's library: host ABI, archives included.
twoine_add_storage(twoine_storage CACHED_FILE_TIMES HIDDEN)
