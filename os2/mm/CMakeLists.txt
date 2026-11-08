# Derived from original 2ine; zlib-style license. See ../../LICENSE.txt.
# Multimedia (IBM w4mm\src\mm): MDM, MMIO and DIVE.
twoine_os2_dll(
    mdm
    mci/mdm/module.c
    mci/mdm/mdm_video.c
    video/decode/video_decode.c
    midi/midi_synth.c
    midi/midi_config.c
    midi/midi_mt32.c
)
twoine_os2_dll(mmio mmio/mmio/module.c)
twoine_os2_dll(dive video/dive/module.c video/dive/dive_pixels.c)
target_link_libraries(dive PRIVATE pmwin)
target_link_libraries(
    mdm
    PRIVATE mmio pmwin m twoine_midi_config twoine_mt32_config twoine_video_config
)
if(TWOINE_MIDI_EXTENDED AND TWOINE_MIDI_SYNTH)
    target_sources(mdm PRIVATE midi/midi_banks.cpp)
    target_compile_features(mdm PRIVATE cxx_std_17)
    set_target_properties(mdm PROPERTIES CXX_VISIBILITY_PRESET hidden)
endif()
if(TWOINE_VIDEO)
    add_dependencies(mdm twoine_ffmpeg_metadata)
endif()
