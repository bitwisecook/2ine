<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# FFmpeg digital-video runtime

User requested an FFmpeg implementation of OS/2's video layer and a fast path
from decoded frames to the display. This is emulator tooling under tools/2ine;
it does not establish reconstruction milestones or original-byte equivalence.
No OS/2 binaries, installed applications or proprietary media were modified.

## Contract evidence

Primary local Toolkit sources examined:

- `/Users/jimd/src/os2/sdk/os2tk45-master/h/mcios2.h`: MCI commands, device type
  12 digital video, 32-bit parameter records, notification status values.
  OPEN base record is 20 bytes, parent extension 24; PLAY 12/speed extension16;
  LOAD and SEEK 8; STATUS/GETDEVCAPS16; PUT/WHERE/WINDOW20. Video rectangle
  records describe offset and size, rather than two opposite corners.
- `h/mmioos2.h`: MMIO IOProc and translated/untranslated media I/O contracts,
  including movie/digital-video media types. `h/codec.h` and `h/mmdrvos2.h`
  describe codec/device interfaces. They are distinct layers below MCI.
- `h/dive.h` and the existing 2ine DIVE implementation describe direct video
  framebuffer access. This change does not claim to add guest DIVE formats.
- `samples/mm/movie/movie.c`: `open digitalvideo alias movie wait shareable`,
  `window movie handle ... wait`, `load movie movie.avi wait`, and `put movie
  destination at x y cx cy wait` form the common movie-player sequence.

Before this change, MDM provided waveform/MIDI/direct mixer playback and MMIO
only retained memory-file elements for audio. It had no digital-video device,
video demuxer or codec dispatcher. The new implementation places the native
FFmpeg service behind the existing MDM MCI boundary. It does not pretend that
MMIO video IOProcs or original codec DLLs are implemented.

## Implementation

- `cmake/Video.cmake`: default pinned FFmpeg source provider, optional system
  provider/offline source. Source archive 9.0.2, official ffmpeg.org release,
  SHA256 `8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`.
  The archive was actually downloaded and hash verified. Source profile was
  configured, compiled and linked; it reports LGPL 2.1+. No auto-detected
  dependencies, network protocols, encoders or command-line tools are enabled.
  Linux native i386 uses explicit -m32 flags; that profile is not locally run.
  Windows source configure is explicitly rejected with an actionable system-
  provider alternative; runtime remains unqualified there.
- `os2libs/video_decode.*`: custom AVIO through HAL file APIs, retained AVFrame
  planes, direct YUV420/NV12/BGRA, cached fallback conversion, seeks, packet
  drain, bounded dimensions/timestamps and stereo float 48 kHz resampling.
- `os2libs/mdm_video.*` plus private string parser: type12/name recognition,
  empty/open+load and direct file open, separate checked wire ABI, sixteen
  digital-video devices starting0x8000, real errors, status/caps, volume,
  ms/frame time formats, destination/window commands, pause/resume/seek,
  worker teardown, WAIT pumping and MCI notify. All native workers are joined
  before decoder/audio/frame-target release. Existing audio IDs stay below
  0x8000. Superseding PLAY notifies1, STOP/CLOSE abort2, errors3, success0.
- Audio has a bounded two-second timestamped stereo ring; actual mixer sample
  consumption drives video, silence represents missing/late samples, expired
  samples are discarded rather than shifting the movie timeline. Explicit
  ranges and known movie duration bound audio padding. Video without audio uses
  monotonic timing. Workers never touch renderer or PM geometry.
- `runtime/audio.{c,h}` adds synchronous generator removal under the existing
  stream lock, waiting for in-flight callbacks before unlinking. Video registers
  one additive stereo generator in the shared runtime mixer and removes it
  before MDM teardown. Device-ring lifetime is protected by the video monitor.
  The parent approved this narrow runtime API addition. A deliberately blocked
  audio callback tests that unregister waits and no later callback occurs;
  MCI shutdown during active movie playback tests stop/unload cleanup.
  MDM's module shutdown also unregisters its original DART/MIDI generator and
  closes remaining audio devices through their existing close path, joining
  delivery threads and releasing guest buffers. The LX fixture deliberately
  leaves an active DART device/buffer at process exit as a cleanup smoke check;
  that alone does not assert the exact DLL deinit call order.
- `pmwin/video_frames.*`: latest-frame ownership mailbox, anchor/window
  teardown invalidation, visible-region clipping, failed upload propagation.
  The PM thread consumes each retained frame exactly once. Existing DIVE
  message-pump sites also pump video; WAIT pumps the same queue/retained path.
- `hal/include/hal/video_frame.h`, `hal/sdl/video.c`: scoped portable HAL,
  reusable streaming textures and direct planar upload, validated stride/span,
  BT601/709 full/limited range, one upload and clipped rendering. Texture is
  destroyed with its host window. No per-pixel PM conversion or fresh texture
  for every frame. This is not a measured hardware-decoder or zero-copy claim.
- `tools/ffmpeg_build_info.c` generates actual linked license/version/config;
  native source catalogue, macOS bundle and GNOME packaging retain it. The
  system Homebrew library was observed to report GPL3+, and that is recorded
  instead of claiming the pinned profile's LGPL license for that provider.

## Validation

Independent build directory: `build-video-review`. Original pinned tar/source
retained in ignored `build-video-review/ffmpeg-9.0.2.tar.xz` and
`build-video-source-input/ffmpeg-9.0.2`. Configuration:

```sh
cmake -S . -B build-video-review -DCMAKE_BUILD_TYPE=Release \
  -DTWOINE_MIDI_EXTENDED=OFF -DTWOINE_PORTABLE_LOADER=ON \
  -DTWOINE_FFMPEG_PROVIDER=source \
  -DTWOINE_FFMPEG_SOURCE_DIR="$PWD/build-video-source-input/ffmpeg-9.0.2" \
  -DTWOINE_SDL3_PROVIDER=system \
  -DTWOINE_FREETYPE_SOURCE_DIR="$PWD/build-image-app-runtime/_deps/freetype-src" \
  -DTWOINE_FONT_DATA_DIR="$PWD/build-image-app-runtime/_deps/twoine_fontdata-src" \
  -DTWOINE_MIDI_DATA_DIR="$PWD/build-image-app-runtime/_deps/midi-data"
cmake --build build-video-review --target twoine_video_tests lx_loader doscalls \
  twoine_dive_tests twoine_mdm_tests -j8
ctest --test-dir build-video-review \
  -R '^(digital_video|digital_video_audio|dive_video|multimedia|multimedia_stereo_disk)$' \
  --output-on-failure -j2
```

The source-profile build completed. `/tmp/2ine-video-final-ctest.log` recorded
5/5 passing checks before the final compressed-fixture addition. Both updated
video checks passed in `/tmp/2ine-video-compressed-ctest.log` (5.06seconds).
`build-video-disabled` configured with `TWOINE_VIDEO=OFF` and built MDM
successfully (`/tmp/2ine-video-disabled.log`). The FFmpeg provider-catalogue
unit test, Ruff, ty, lxapigen consistency and source-boundary checks passed.
Final acceptance after colorspace/range and retained NV12 tests passed5/5 in
6.54seconds (`/tmp/2ine-video-accept-ctest.log`).
The shared-mixer implementation, capability item/message queries and callback
lifetime regression passed5/5 in6.78seconds (`/tmp/2ine-video-shared-ctest.log`).
The complete module wrapper and latest source-profile tests passed5/5 in
6.68seconds (`/tmp/2ine-video-final-mix-ctest.log`). Source formatter, Ruff,
ty, binding/catalogue consistency, source boundaries and whitespace checks pass.

`tests/integration/video.py` builds original 64x48 four-frame movies at10fps:
raw BGR24 AVI, direct I420 AVI, stereo PCM44100 AVI and Cinepak AVI. Cinepak's
small codebook packets were generated from those same original flat-color
frames using the local FFmpeg9.0.2 encoder, then retained compactly so CI needs
no executable. C tests check four exact PTS values0/100/200/300ms, duration400,
seek/EOF, decoded red pixels, clipped SDL pixels and invalid plane rejection.
Retained frame replacement/upload/error checks count exact owner releases.
Audio independently decodes to19,200 stereo frames with L/R energy599.968/37.488.
The disk backend verifies finite float samples and the actual host stereo
energy ratio. Native MCI tests exercise async PLAY, pause/resume, frozen
position, WAIT duration, seek, empty OPEN/LOAD, bad LOAD preservation, text
commands and offset-size WHERE roundtrip. A generated real LX program calls
MDM ordinals1/2, checks numeric type12, LOAD's8-byte bridge, status, WAIT,
canaries and invalid pointers. Renderer tests use SDL's software/dummy backend;
physical GPU timing and human visual smoothness are not established by them.

## Limits / next investigation

No original commercial movie-player workflow was run. Guest MMIO video
IOProc/codec APIs, source rectangles, editing/recording, subtitles, hardware
decode and true overlay surfaces are not implemented; unsupported requests
must remain errors. Modern codec inclusion is compile coverage, not a full
per-codec compatibility corpus. The retained path is available to native video,
while guest DIVE still follows its existing separate implementation. Unusual
containers whose audio/video interleave exceeds the bounded ring need further
real-file testing. Linux/GNOME and Windows playback remain unqualified here.
