# Public domain. No rights reserved. Wholly AI-authored.
option(TWOINE_MIDI_EXTENDED "Play SF2/SF3/DLS and SFZ banks with channel routing" ON)
target_compile_definitions(twoine_midi_config INTERFACE TWOINE_MIDI_EXTENDED=$<BOOL:${TWOINE_MIDI_EXTENDED}>)
if(NOT TWOINE_MIDI_EXTENDED)
    return()
endif()
enable_language(CXX)
include(ExternalProject)
set(TWOINE_FLUIDSYNTH_SOURCE_DIR "" CACHE PATH "Offline FluidSynth 2.4.8 source")
set(TWOINE_LIQUIDSFZ_SOURCE_DIR "" CACHE PATH "Offline liquidsfz 0.4.2 source")
set(TWOINE_INSTPATCH_SOURCE_DIR "" CACHE PATH "Offline libInstPatch 1.1.7 source")
function(twoine_check_midi_source directory prefix expected)
    file(READ "${directory}/CMakeLists.txt" content)
    foreach(component MAJOR MINOR MICRO)
        string(REGEX MATCH "set[ \t]*\\([ \t]*${prefix}_VERSION_${component}[ \t]+([0-9]+)" match "${content}")
        set(version_${component} "${CMAKE_MATCH_1}")
    endforeach()
    if(NOT "${version_MAJOR}.${version_MINOR}.${version_MICRO}" STREQUAL expected)
        message(FATAL_ERROR "MIDI source override must contain ${prefix} ${expected}")
    endif()
endfunction()
if(TWOINE_FLUIDSYNTH_SOURCE_DIR)
    twoine_check_midi_source("${TWOINE_FLUIDSYNTH_SOURCE_DIR}" FLUIDSYNTH 2.4.8)
endif()
if(TWOINE_INSTPATCH_SOURCE_DIR)
    twoine_check_midi_source("${TWOINE_INSTPATCH_SOURCE_DIR}" IPATCH 1.1.7)
endif()
if(TWOINE_LIQUIDSFZ_SOURCE_DIR)
    file(READ "${TWOINE_LIQUIDSFZ_SOURCE_DIR}/configure.ac" _sfz_configuration)
    if(NOT _sfz_configuration MATCHES "AC_INIT.*liquidsfz.*0[.]4[.]2")
        message(FATAL_ERROR "TWOINE_LIQUIDSFZ_SOURCE_DIR must contain liquidsfz 0.4.2")
    endif()
endif()
set(_midi_prefix "${PROJECT_BINARY_DIR}/_deps/midi-install")
set(_midi_flags "-O2 -fPIC")
set(_midi_environment "PKG_CONFIG_PATH=${_midi_prefix}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
if(TWOINE_NATIVE_LOADER)
    string(APPEND _midi_flags " -m32")
    list(APPEND _midi_environment "PKG_CONFIG_LIBDIR=/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig")
endif()
file(MAKE_DIRECTORY "${_midi_prefix}/include/fluidsynth")
if(TWOINE_FLUIDSYNTH_SOURCE_DIR)
    set(_fluid_source SOURCE_DIR "${TWOINE_FLUIDSYNTH_SOURCE_DIR}")
else()
    set(_fluid_source URL https://github.com/FluidSynth/fluidsynth/archive/refs/tags/v2.4.8.tar.gz
        URL_HASH SHA256=d6d04e9014557906ee42aa1fe3cd580840ea482d4c3b1a45fddd45ec606d6e97)
endif()
set(_fluid_options "")
foreach(feature alsa aufile dbus jack ladspa midishare network oss dsound wasapi waveout winmidi sdl3 pulseaudio pipewire readline openmp coreaudio coremidi framework signalsmith systemd)
    list(APPEND _fluid_options "-Denable-${feature}=OFF")
endforeach()

if(TWOINE_INSTPATCH_SOURCE_DIR)
    set(_instpatch_source SOURCE_DIR "${TWOINE_INSTPATCH_SOURCE_DIR}")
else()
    set(_instpatch_source URL https://github.com/swami/libinstpatch/archive/refs/tags/v1.1.7.tar.gz
        URL_HASH SHA256=b388ab6f843559fc2da94837c37dfd4cf5973cf7cc2a0ce3cb33260b81377e9f)
endif()
ExternalProject_Add(twoine_instpatch_dependency ${_instpatch_source}
    CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env ${_midi_environment}
        "${CMAKE_COMMAND}" -S <SOURCE_DIR> -B <BINARY_DIR>
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        "-DCMAKE_C_FLAGS=${_midi_flags}"
        -DCMAKE_INSTALL_PREFIX=${_midi_prefix} -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_INSTALL_RPATH=${_midi_prefix}/lib
        -DBUILD_SHARED_LIBS=ON -DINTROSPECTION_ENABLED=OFF -DGTKDOC_ENABLED=OFF
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel 4
    INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR>)
ExternalProject_Add(twoine_fluidsynth_dependency ${_fluid_source}
    DEPENDS twoine_instpatch_dependency
    CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env ${_midi_environment}
        "${CMAKE_COMMAND}" -S <SOURCE_DIR> -B <BINARY_DIR>
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        "-DCMAKE_C_FLAGS=${_midi_flags}" "-DCMAKE_CXX_FLAGS=${_midi_flags}"
        -DCMAKE_INSTALL_PREFIX=${_midi_prefix} -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_INSTALL_RPATH=${_midi_prefix}/lib
        -DBUILD_SHARED_LIBS=ON -Denable-libsndfile=ON -Denable-libinstpatch=ON
        -Denable-threads=ON -DBUILD_TESTING=OFF ${_fluid_options}
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel 4
    INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR>
    BUILD_BYPRODUCTS "${_midi_prefix}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}fluidsynth${CMAKE_SHARED_LIBRARY_SUFFIX}")
if(TWOINE_LIQUIDSFZ_SOURCE_DIR)
    set(_sfz_source SOURCE_DIR "${TWOINE_LIQUIDSFZ_SOURCE_DIR}")
else()
    set(_sfz_source URL https://github.com/swesterfeld/liquidsfz/releases/download/0.4.2/liquidsfz-0.4.2.tar.bz2
        URL_HASH SHA256=fd93c122e3f92c83e82cd76050416d4e77c7bab3a91c84315bc249618d3d6e6e)
endif()
find_program(TWOINE_DEPENDENCY_MAKE NAMES gmake make REQUIRED)
ExternalProject_Add(twoine_liquidsfz_dependency ${_sfz_source}
    CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env ${_midi_environment}
        "CC=${CMAKE_C_COMPILER}" "CXX=${CMAKE_CXX_COMPILER}"
        "CFLAGS=${_midi_flags}" "CXXFLAGS=${_midi_flags}"
        <SOURCE_DIR>/configure --prefix=${_midi_prefix} --libdir=${_midi_prefix}/lib
        --without-jack --without-lv2 --without-fftw --enable-shared --disable-static
    BUILD_COMMAND "${TWOINE_DEPENDENCY_MAKE}" -j4 -C lib
    INSTALL_COMMAND "${TWOINE_DEPENDENCY_MAKE}" -C lib install
    BUILD_BYPRODUCTS "${_midi_prefix}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}liquidsfz${CMAKE_SHARED_LIBRARY_SUFFIX}")
foreach(dependency fluidsynth liquidsfz)
    add_library(twoine_${dependency} SHARED IMPORTED GLOBAL)
    set_target_properties(twoine_${dependency} PROPERTIES
        IMPORTED_LOCATION "${_midi_prefix}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}${dependency}${CMAKE_SHARED_LIBRARY_SUFFIX}"
        INTERFACE_INCLUDE_DIRECTORIES "${_midi_prefix}/include")
    add_dependencies(twoine_${dependency} twoine_${dependency}_dependency)
endforeach()
target_link_libraries(twoine_midi_config INTERFACE twoine_fluidsynth twoine_liquidsfz)
