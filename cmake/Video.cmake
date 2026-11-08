# Public domain. No rights reserved. Wholly AI-authored.
option(TWOINE_VIDEO "Decode OS/2 digital video with FFmpeg" ON)
set(TWOINE_FFMPEG_PROVIDER "source" CACHE STRING "FFmpeg dependency: source or system")
set_property(CACHE TWOINE_FFMPEG_PROVIDER PROPERTY STRINGS source system)
set(TWOINE_FFMPEG_SOURCE_DIR "" CACHE PATH "Offline FFmpeg 9.0.2 source")
add_library(twoine_video_config INTERFACE)
target_compile_definitions(twoine_video_config INTERFACE TWOINE_VIDEO=$<BOOL:${TWOINE_VIDEO}>)
if(NOT TWOINE_VIDEO)
    file(REMOVE "${PROJECT_BINARY_DIR}/ffmpeg-build.json")
    return()
endif()
if(TWOINE_FFMPEG_PROVIDER STREQUAL "system")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(
        TWOINE_FFMPEG
        REQUIRED
        IMPORTED_TARGET
        GLOBAL
        libavformat>=60
        libavcodec>=60
        libavutil>=58
        libswscale>=7
        libswresample>=4
    )
    target_link_libraries(twoine_video_config INTERFACE PkgConfig::TWOINE_FFMPEG)
else()
    if(NOT TWOINE_FFMPEG_PROVIDER STREQUAL "source")
        message(FATAL_ERROR "TWOINE_FFMPEG_PROVIDER must be source or system")
    endif()
    include(ExternalProject)
    find_program(TWOINE_FFMPEG_MAKE NAMES gmake make REQUIRED)
    if(TWOINE_FFMPEG_SOURCE_DIR)
        file(READ "${TWOINE_FFMPEG_SOURCE_DIR}/RELEASE" _ffmpeg_release)
        string(STRIP "${_ffmpeg_release}" _ffmpeg_release)
        if(NOT _ffmpeg_release STREQUAL "9.0.2")
            message(FATAL_ERROR "Offline FFmpeg source must be 9.0.2")
        endif()
        set(_ffmpeg_source SOURCE_DIR "${TWOINE_FFMPEG_SOURCE_DIR}")
    else()
        set(_ffmpeg_source
            URL
            https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz
            URL_HASH
            SHA256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
        )
    endif()
    set(_ffmpeg_prefix "${PROJECT_BINARY_DIR}/_deps/ffmpeg-install")
    file(MAKE_DIRECTORY "${_ffmpeg_prefix}/include")
    set(_ffmpeg_extra "")
    if(TWOINE_NATIVE_LOADER)
        list(APPEND _ffmpeg_extra --arch=x86 --cpu=i686 --extra-cflags=-m32 --extra-ldflags=-m32)
    endif()
    if(WIN32)
        message(
            FATAL_ERROR
            "Pinned FFmpeg source builds currently require Unix configure tools. Use TWOINE_FFMPEG_PROVIDER=system with matching Windows FFmpeg libraries."
        )
    endif()
    ExternalProject_Add(
        twoine_ffmpeg_dependency
        ${_ffmpeg_source}
        CONFIGURE_COMMAND
            <SOURCE_DIR>/configure --prefix=${_ffmpeg_prefix} --cc=${CMAKE_C_COMPILER}
            --disable-autodetect --disable-everything --disable-programs --disable-doc
            --disable-network --disable-shared --disable-avdevice --disable-avfilter --enable-static
            --enable-pic --disable-x86asm --enable-pthreads --enable-avformat --enable-avcodec
            --enable-avutil --enable-swscale --enable-swresample
            --enable-demuxer=avi,mov,mpegps,mpegts,matroska,gif,flic
            --enable-decoder=rawvideo,msrle,msvideo1,cinepak,indeo2,indeo3,indeo4,indeo5,mjpeg,mpeg1video,mpeg2video,mpeg4,h264,hevc,vp8,vp9,ffv1,gif,flic,pcm_u8,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s32le,pcm_f32le,adpcm_ms,adpcm_ima_wav,mp3,mp2,aac,ac3,vorbis,opus,flac
            --enable-parser=mpegvideo,mpeg4video,h264,hevc,vp8,vp9,mpegaudio,aac,ac3,opus
            ${_ffmpeg_extra}
        BUILD_COMMAND "${TWOINE_FFMPEG_MAKE}" -j4
        INSTALL_COMMAND "${TWOINE_FFMPEG_MAKE}" install
        BUILD_BYPRODUCTS
            "${_ffmpeg_prefix}/lib/libavformat.a"
            "${_ffmpeg_prefix}/lib/libavcodec.a"
            "${_ffmpeg_prefix}/lib/libswscale.a"
            "${_ffmpeg_prefix}/lib/libswresample.a"
            "${_ffmpeg_prefix}/lib/libavutil.a"
    )
    foreach(
        component
        avformat
        avcodec
        swscale
        swresample
        avutil
    )
        add_library(twoine_ffmpeg_${component} STATIC IMPORTED GLOBAL)
        set_target_properties(
            twoine_ffmpeg_${component}
            PROPERTIES
                IMPORTED_LOCATION "${_ffmpeg_prefix}/lib/lib${component}.a"
                INTERFACE_INCLUDE_DIRECTORIES "${_ffmpeg_prefix}/include"
        )
        add_dependencies(twoine_ffmpeg_${component} twoine_ffmpeg_dependency)
        target_link_libraries(twoine_video_config INTERFACE twoine_ffmpeg_${component})
    endforeach()
    target_link_libraries(twoine_video_config INTERFACE Threads::Threads m)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Static codec tables referenced by PC-relative assembly are private to
        # the consuming module, not interposable ELF exports.
        target_link_options(twoine_video_config INTERFACE "LINKER:--exclude-libs,ALL")
    endif()
endif() # source provider
add_executable(twoine_ffmpeg_info "${PROJECT_SOURCE_DIR}/tools/ffmpeg_build_info.c")
twoine_application_target(twoine_ffmpeg_info)
target_link_libraries(twoine_ffmpeg_info PRIVATE twoine_video_config)
add_custom_command(
    OUTPUT "${PROJECT_BINARY_DIR}/ffmpeg-build.json"
    COMMAND twoine_ffmpeg_info "${PROJECT_BINARY_DIR}/ffmpeg-build.json" "${TWOINE_FFMPEG_PROVIDER}"
    DEPENDS twoine_ffmpeg_info
    VERBATIM
)
add_custom_target(twoine_ffmpeg_metadata ALL DEPENDS "${PROJECT_BINARY_DIR}/ffmpeg-build.json")
