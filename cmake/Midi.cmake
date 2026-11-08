# Public domain. No rights reserved. Wholly AI-authored.
option(TWOINE_MIDI_SYNTH "Render General MIDI with TinySoundFont and a complete sampled bank" ON)
set(TWOINE_MIDI_DATA_DIR "" CACHE PATH "Offline pinned TinySoundFont and GeneralUser GS files")
add_library(twoine_midi_config INTERFACE)
if(NOT TWOINE_MIDI_SYNTH)
    target_compile_definitions(
        twoine_midi_config
        INTERFACE TWOINE_MIDI_SYNTH=0 TWOINE_MIDI_EXTENDED=0
    )
    message(WARNING "General MIDI synthesis disabled: music uses the basic oscillator fallback")
    return()
endif()
set(_twoine_tsf_revision 853a0a171759f1ddba0de1442133a75912bbeffa)
if(TWOINE_MIDI_DATA_DIR)
    set(_twoine_midi_data "${TWOINE_MIDI_DATA_DIR}")
else()
    set(_twoine_midi_data "${PROJECT_BINARY_DIR}/_deps/midi-data")
endif()
function(twoine_midi_input filename url hash)
    set(path "${_twoine_midi_data}/${filename}")
    if(NOT EXISTS "${path}")
        if(TWOINE_MIDI_DATA_DIR)
            message(FATAL_ERROR "Missing offline MIDI dependency: ${path}")
        endif()
        file(MAKE_DIRECTORY "${_twoine_midi_data}")
        file(DOWNLOAD "${url}" "${path}" EXPECTED_HASH "SHA256=${hash}" TLS_VERIFY ON STATUS result)
        list(GET result 0 code)
        if(NOT code EQUAL 0)
            message(FATAL_ERROR "MIDI dependency download failed: ${filename}: ${result}")
        endif()
    endif()
    file(SHA256 "${path}" actual)
    if(NOT actual STREQUAL hash)
        message(FATAL_ERROR "MIDI dependency hash mismatch: ${path}")
    endif()
endfunction()
twoine_midi_input(
    tsf.h
    "https://raw.githubusercontent.com/schellingb/TinySoundFont/${_twoine_tsf_revision}/tsf.h"
    70d55963c98f60ebb81518eaa1f25d46888d5180eb5f5289fd6b74ffc177d197
)
twoine_midi_input(
    TinySoundFont-LICENSE
    "https://raw.githubusercontent.com/schellingb/TinySoundFont/${_twoine_tsf_revision}/LICENSE"
    ef10cb839de73fb6d5d32ea7151979e87809a6df70dc7e7ee8457ae1c9e7aa7e
)
twoine_midi_input(
    FluidR3_GM.sf2
    "https://github.com/pianobooster/fluid-soundfont/releases/download/v3.1/FluidR3_GM.sf2"
    74594e8f4250680adf590507a306655a299935343583256f3b722c48a1bc1cb0
)
twoine_midi_input(
    FluidR3-MIT.txt
    "https://raw.githubusercontent.com/pianobooster/fluid-soundfont/v3.1/COPYING"
    8ef830b65c97a976b86e34bb5fde08d99dfb1db13c4149b5b20bc837ac6c4568
)
twoine_midi_input(
    FluidR3-README.txt
    "https://raw.githubusercontent.com/pianobooster/fluid-soundfont/v3.1/README"
    2f92d462684629f207e43815115b79d7f1ce130d30ca8792b7d82b398a8306f9
)
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/soundfonts")
file(
    REMOVE
    "${PROJECT_BINARY_DIR}/soundfonts/GeneralUser-GS.sf2"
    "${PROJECT_BINARY_DIR}/soundfonts/GeneralUser-GS-LICENSE.txt"
)
foreach(filename FluidR3_GM.sf2 FluidR3-MIT.txt FluidR3-README.txt TinySoundFont-LICENSE)
    configure_file(
        "${_twoine_midi_data}/${filename}"
        "${PROJECT_BINARY_DIR}/soundfonts/${filename}"
        COPYONLY
    )
endforeach()
target_include_directories(twoine_midi_config SYSTEM INTERFACE "${_twoine_midi_data}")
target_compile_definitions(
    twoine_midi_config
    INTERFACE
        TWOINE_MIDI_SYNTH=1
        TWOINE_SOUNDFONT_FILE="${PROJECT_BINARY_DIR}/soundfonts/FluidR3_GM.sf2"
)

include("${PROJECT_SOURCE_DIR}/cmake/MidiBanks.cmake")
