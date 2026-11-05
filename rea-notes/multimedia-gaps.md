# Multimedia API inventory

Read-only source/SDK inventory, 2026-11-03. This is a compatibility gap survey,
not a new implementation commitment or full application conformance result.
The local authority is `sdk/os2tk45-master/h` in the OS/2 workspace.

| Area | Implemented path | Remaining native gaps | Status of originals |
| --- | --- | --- | --- |
| MMIO/IOProcs/codecs | `os2libs/mmio.c` exports only memory-backed mmioOpen/Close; an internal owned-copy helper feeds MIDI | Named files, mmioRead/Seek/Write, chunk/container/header and format discovery, IOProc registration, installation and codec-procedure loading from `mmioos2.h` | User IOProc/codec DLLs are not qualified simply because the LX loader can read them. FFmpeg is currently behind MCI, not a general replacement MMIO codec API. |
| MCI/DART PCM | `mdm.c`: type 9 AMPMIX playback, MIXSETUP/buffer/write callback path, 8/16-bit mono/stereo at 4–192 kHz, volume and lifetime handling | Waveform-file type 7/general sequencer type 8 device surfaces, full STATUS/GETDEVCAPS and device discovery, CD/CDXA/overlay/tape and mixer/connector controls; recording/input/capture; high-level mciPlayFile/Resource/record macros in `mciapi.h` | Original high-level/device DLLs may add user-mode logic, but depend on missing MDM/SSM/driver services and are unqualified. |
| MIDI synthesis | Standard MIDI Files through memory MMIO/MCI, sampled GM, optional extended bank formats and Munt MT-32, shared stereo mixer | RTMIDI live node/instance/link/port APIs, incoming MIDI, external hardware output, routing/retrieval/timers from `mididll.h` | Original RTMIDI.DLL is present in the full extracted OS/2 media. There is no native rtmidi module, and actual driver-backed operation has not been qualified. |
| SSM/streaming | Internal movie audio/video scheduling and DART queues | Public SpiCreate/Start/Stop/SeekStream, handler enumeration, stream association/synchronization/event graph from `ssm.h` | Original SSM.DLL is present in extracted media; no native SSM/Spi implementation is built, and original service/driver dependencies are unqualified. |
| Digital video | `mdm_video.c`/`video_decode.c`: type 12 digitalvideo open/load/window/play/pause/resume/seek/stop, time/status/caps subset, notifications, FFmpeg software decoding, synchronized movie audio; retained YUV/NV12/BGRA upload path | Capture/recording/editing/save, full MMPM device-manager and codec SPI, hardware decoding, streaming URLs/network transports; Windows source-provider/toolchain playback remains unqualified | Original players may use the covered MCI subset; that does not qualify every installed player or original codec DLL. |
| DIVE/direct video | Public ordinals 1–19; 20 input/11 output formats, short setup records, offscreen conversion, palettes, transparent/line-mask blits and a mapped linear software framebuffer through PM | GPU conversion/scaling, YUV output/GREY/GY16, overlays, complete desktop occlusion, physical VRAM and actual HWVIDEO/device acceleration | No native HWVIDEO/VMAN implementation. Fullscreen entries remain external mode-change notifications. See [graphics completion](graphics-completion.md) and [DIVE contracts](../docs/dive.md). |
| OpenGL/PGL | 336 core GL functions, 1.1 guest surface, Mesa/EGL/PGL context/window/share/swap and PM integration | Empty exposed GL extension list; no modern GL/Vulkan/DirectX guest API, color-index/stereo/aux/accum visuals; color-index palette selection; no supplied GLU/GLUT/AUX. Context-copy, font-list and front-bitmap helpers now have behavior and rendering tests | Original OPENGL is an explicit user choice and unqualified. Compatible guest helper libraries may be loadable with the chosen provider, but their presence is not proof of compatibility. |
| GPI/bitmap/metafile graphics | PM text/shapes/raster subset; new QueryBitmapBits 599/DrawBits 603 handles the original SmartMaster upload and tab masks | Metafile load/play/edit and advanced retained graphics/matrix/boundary controls are partial or return explicit unsupported errors; raw bitmap palette-index/no-color-info flags, compression, multi-plane data and mirroring remain outside the new path | Original applications using advanced GPI still need native API coverage. PMGRE's exposed low-level driver gateways return 0; they are unavailable fallbacks, not implemented display-driver services. |
| Gaming/input | Keyboard/mouse/relative capture, timers, DART/MIDI, DIVE and optional GL paths used by existing Doom/GLQuake verification | No native joystick/gamepad/gameport mapping or general game-device IOCTL translation found in HAL/native sources; no native hardware audio/video driver emulation | DOS/Win-OS2 or third-party game-library/hardware-driver paths are separate compatibility questions and unqualified by current OS/2 user-mode tests. |

The native build's module list in `os2libs/CMakeLists.txt` contains MDM, MMIO,
DIVE and optional OpenGL. It does not build RTMIDI, SSM or HWVIDEO. This distinguishes
missing modules from partial real implementations and from PMGRE's exposed
unsupported gateways. Native placeholder help/SOM paths are separate system
services; successful original help rendering is not multimedia-driver coverage.

## Priority for application compatibility

1. Complete the public MMIO file/header/format/IOProc/codec contracts. This is the
   largest distinction between native MCI playback and the wider MMPM ecosystem.
2. Expand general MCI wave/sequencer/mixer discovery and device commands, then
   capture/recording or SSM/RTMIDI according to real application evidence.
3. Add DIVE formats and game/controller input where actual titles require them.
4. Extend GL/PGL or advanced GPI/metafile coverage based on concrete import/call
   failures. A newer host Mesa alone does not expose new guest GL extensions.

Useful concrete sources: `os2libs/{mmio,mdm,dive,pmgre}.c`,
`os2libs/mdm_video.c`, `os2libs/video_decode.c`, `os2libs/opengl/{custom,pgl}.c`,
`os2libs/pmgpi/{help,bitmap}.c`, `os2libs/doscalls/files.c` (DevIOCtl),
`cmake/Video.cmake`, `docs/{audio,video,opengl}.md`.
SDK contracts: `mmioos2.h`, `mcios2.h`, `mciapi.h`, `mididll.h`, `ssm.h`,
`dive.h`, `pmgpi.h`, `pmdev.h`. Source searches found no native RTMIDI/HWVIDEO,
joystick/gamepad or WinMM/DirectSound/DirectDraw API implementations; those
negative findings are bounded to the current `os2libs`, `guest/bindings` and
`hal` trees. Original DLLs were inventoried, not executed for this survey.
