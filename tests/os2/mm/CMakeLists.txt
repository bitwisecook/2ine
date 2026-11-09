# Public domain. No rights reserved. Wholly AI-authored.
# Multimedia: DIVE, MMPM (MCI, DART, MIDI) and digital video.
if(TARGET dive)
    add_executable(twoine_dive_tests test_dive.c)
    add_executable(twoine_dive_format_tests test_dive_formats.c)
    twoine_application_target(twoine_dive_format_tests)
    target_link_libraries(twoine_dive_format_tests PRIVATE dive)
    add_test(NAME dive_formats COMMAND twoine_dive_format_tests)
    twoine_application_target(twoine_dive_tests)
    target_link_libraries(twoine_dive_tests PRIVATE dive pmmerge 2ine)
    add_test(NAME dive_video COMMAND twoine_dive_tests)
    set_tests_properties(
        dive_video
        PROPERTIES
            TIMEOUT 20
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
    )
endif()
if(TARGET mmpm)
    add_executable(twoine_mdm_tests test_mdm.c)
    twoine_application_target(twoine_mdm_tests)
    target_link_libraries(twoine_mdm_tests PRIVATE mmpm 2ine m)
    add_test(NAME multimedia COMMAND twoine_mdm_tests)
    set_tests_properties(
        multimedia
        PROPERTIES
            TIMEOUT 20
            ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
    )
    add_test(NAME multimedia_stereo_disk COMMAND twoine_mdm_tests)
    set_tests_properties(
        multimedia_stereo_disk
        PROPERTIES
            TIMEOUT 20
            ENVIRONMENT
                "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=disk;SDL_RENDER_DRIVER=software;SDL_AUDIO_DISK_OUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/mdm-stereo.raw;TWOINE_TEST_STEREO_CAPTURE=${CMAKE_CURRENT_BINARY_DIR}/mdm-stereo.raw"
    )
endif()

if(TARGET mmpm AND TWOINE_MIDI_SYNTH)
    add_executable(twoine_midi_synth_tests test_midi_synth.c)
    twoine_application_target(twoine_midi_synth_tests)
    target_link_libraries(twoine_midi_synth_tests PRIVATE twoine_os2_midi 2ine)
    if(TWOINE_MIDI_EXTENDED)
        # The synthesizer library holds C++; link with the C++ driver.
        set_target_properties(twoine_midi_synth_tests PROPERTIES LINKER_LANGUAGE CXX)
        add_executable(twoine_midi_bank_tests test_midi_banks.c)
        twoine_application_target(twoine_midi_bank_tests)
        set_target_properties(twoine_midi_bank_tests PROPERTIES LINKER_LANGUAGE CXX)
        target_link_libraries(twoine_midi_bank_tests PRIVATE twoine_os2_midi 2ine)
        add_test(
            NAME midi_bank_formats
            COMMAND
                "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/integration/os2/mm/midi_banks.py" --build
                "${PROJECT_BINARY_DIR}"
        )
        set_tests_properties(midi_bank_formats PROPERTIES TIMEOUT 120)
    endif()
    add_test(NAME midi_instruments COMMAND twoine_midi_synth_tests)
    set_tests_properties(
        midi_instruments
        PROPERTIES
            TIMEOUT 30
            ENVIRONMENT
                "TWOINE_SOUNDFONT=${PROJECT_BINARY_DIR}/soundfonts/FluidR3_GM.sf2;TWOINE_AUDIO_CONFIG=${PROJECT_BINARY_DIR}/no-user-audio.conf"
    )
endif()

if(TARGET mmpm AND UNIX)
    add_executable(twoine_midi_config_tests test_midi_config.c)
    twoine_application_target(twoine_midi_config_tests)
    target_link_libraries(twoine_midi_config_tests PRIVATE twoine_os2_midi 2ine)
    add_test(NAME midi_preferences COMMAND twoine_midi_config_tests)
    set_tests_properties(midi_preferences PROPERTIES TIMEOUT 20)
endif()

if(TARGET mmpm AND UNIX)
    add_executable(
        twoine_midi_mt32_tests
        test_midi_mt32.c
        ${PROJECT_SOURCE_DIR}/os2/mm/midi/midi_mt32.c
        ${PROJECT_SOURCE_DIR}/os2/mm/midi/midi_config.c
    )
    twoine_application_target(twoine_midi_mt32_tests)
    target_link_libraries(twoine_midi_mt32_tests PRIVATE twoine_mt32_config 2ine m)
    add_test(NAME midi_mt32_availability COMMAND twoine_midi_mt32_tests)
    set_tests_properties(midi_mt32_availability PROPERTIES TIMEOUT 20)
endif()

if(TARGET mmpm AND TWOINE_VIDEO)
    add_executable(twoine_video_tests test_video.c)
    twoine_application_target(twoine_video_tests)
    target_link_libraries(twoine_video_tests PRIVATE mmpm pmmerge 2ine)
    add_test(
        NAME digital_video
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/mm/video.py" --build
            "${PROJECT_BINARY_DIR}" --work "${PROJECT_BINARY_DIR}/video-tests" --backend
            "${_twoine_legacy_backend}"
    )
    set_tests_properties(digital_video PROPERTIES TIMEOUT 40)
    add_test(
        NAME digital_video_audio
        COMMAND
            "${Python3_EXECUTABLE}" "${TWOINE_TEST_SOURCE_DIR}/integration/os2/mm/video.py" --build
            "${PROJECT_BINARY_DIR}" --work "${PROJECT_BINARY_DIR}/video-audio-tests" --backend
            "${_twoine_legacy_backend}" --audio-driver disk
    )
    set_tests_properties(digital_video_audio PROPERTIES TIMEOUT 40)
endif()
