# Derived from original 2ine; zlib-style license. See ../../LICENSE.txt.
# Multimedia (IBM w4mm\src\mm): MDM, MMIO and DIVE.
# 2ine's software synthesizer (FluidSynth, liquidsfz, Munt) and FFmpeg video
# decoder behind MDM's sequencer and digital-video devices. Object libraries keep
# their symbols in MDM as before; the tests link the same objects.
add_library(twoine_os2_midi OBJECT midi/midi_synth.c midi/midi_config.c midi/midi_mt32.c)
twoine_application_target(twoine_os2_midi)
set_target_properties(twoine_os2_midi PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(twoine_os2_midi PUBLIC twoine_midi_config twoine_mt32_config m)
if(TWOINE_MIDI_EXTENDED AND TWOINE_MIDI_SYNTH)
    target_sources(twoine_os2_midi PRIVATE midi/midi_banks.cpp)
    target_compile_features(twoine_os2_midi PRIVATE cxx_std_17)
    set_target_properties(twoine_os2_midi PROPERTIES CXX_VISIBILITY_PRESET hidden)
endif()
add_library(twoine_os2_video_decode OBJECT video/decode/video_decode.c)
twoine_application_target(twoine_os2_video_decode)
set_target_properties(twoine_os2_video_decode PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(twoine_os2_video_decode PUBLIC twoine_video_config)
if(TWOINE_VIDEO)
    add_dependencies(twoine_os2_video_decode twoine_ffmpeg_metadata)
endif()

twoine_os2_dll(mdm mci/mdm/module.c mci/mdm/mdm_video.c)
twoine_os2_dll(mmio mmio/mmio/module.c)
twoine_os2_dll(dive video/dive/module.c video/dive/dive_pixels.c)
target_link_libraries(dive PRIVATE pmwin)
target_link_libraries(mdm PRIVATE mmio pmwin twoine_os2_midi twoine_os2_video_decode)
if(TWOINE_MIDI_EXTENDED AND TWOINE_MIDI_SYNTH)
    set_target_properties(mdm PROPERTIES LINKER_LANGUAGE CXX)
endif()
if(TWOINE_VIDEO)
    add_dependencies(mdm twoine_ffmpeg_metadata)
endif()
