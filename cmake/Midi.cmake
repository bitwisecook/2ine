# Public domain. No rights reserved. Wholly AI-authored.
option(TWOINE_MIDI_SYNTH "Render General MIDI with TinySoundFont and a complete sampled bank" ON)
set(TWOINE_MIDI_DATA_DIR "" CACHE PATH "Offline pinned TinySoundFont and GeneralUser GS files")
add_library(twoine_midi_config INTERFACE)
if(NOT TWOINE_MIDI_SYNTH)
    target_compile_definitions(twoine_midi_config INTERFACE TWOINE_MIDI_SYNTH=0)
    message(WARNING "General MIDI synthesis disabled: music uses the basic oscillator fallback")
    return()
endif()
set(_twoine_tsf_revision 853a0a171759f1ddba0de1442133a75912bbeffa)
set(_twoine_bank_revision 684543d5e5efaef08d02be50dcda8d552478fa60)
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
        file(DOWNLOAD "${url}" "${path}" EXPECTED_HASH "SHA256=${hash}" TLS_VERIFY ON
            STATUS result)
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
twoine_midi_input(tsf.h
    "https://raw.githubusercontent.com/schellingb/TinySoundFont/${_twoine_tsf_revision}/tsf.h"
    70d55963c98f60ebb81518eaa1f25d46888d5180eb5f5289fd6b74ffc177d197)
twoine_midi_input(TinySoundFont-LICENSE
    "https://raw.githubusercontent.com/schellingb/TinySoundFont/${_twoine_tsf_revision}/LICENSE"
    ef10cb839de73fb6d5d32ea7151979e87809a6df70dc7e7ee8457ae1c9e7aa7e)
twoine_midi_input(GeneralUser-GS.sf2
    "https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/${_twoine_bank_revision}/GeneralUser-GS.sf2"
    9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe)
twoine_midi_input(GeneralUser-GS-LICENSE.txt
    "https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/${_twoine_bank_revision}/documentation/LICENSE.txt"
    7b32efefdf95ce38a043799f0659853ddc00fbaa14d8c50f0aca16b9b8b405be)
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/soundfonts")
foreach(filename GeneralUser-GS.sf2 GeneralUser-GS-LICENSE.txt TinySoundFont-LICENSE)
    configure_file("${_twoine_midi_data}/${filename}"
        "${PROJECT_BINARY_DIR}/soundfonts/${filename}" COPYONLY)
endforeach()
target_include_directories(twoine_midi_config SYSTEM INTERFACE "${_twoine_midi_data}")
target_compile_definitions(twoine_midi_config INTERFACE TWOINE_MIDI_SYNTH=1
    TWOINE_SOUNDFONT_FILE="${PROJECT_BINARY_DIR}/soundfonts/GeneralUser-GS.sf2")
