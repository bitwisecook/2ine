<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Digital-video playback

2ine implements OS/2's MMPM/2 digital-video device with FFmpeg. Applications
using `MDM!mciSendCommand` or `mciSendString` can open a local movie, attach a PM
window, play with synchronized stereo sound, pause, resume, seek and close it.
The normal OS/2 device type is **12**, or `digitalvideo` in text commands.

Both opening a filename directly and the Toolkit movie-player sequence work:

```text
open digitalvideo alias movie wait shareable
window movie handle 65537 wait
load movie "C:\VIDEO\DEMO.AVI" wait
put movie destination at 0 0 640 480 wait
play movie wait
close movie wait
```

Use the application's real PM window handle in place of `65537`. When no
window is supplied, playback creates a standard PM video window. Destination
rectangles use an offset and size. Milliseconds and frame time formats,
length/position/mode/ready/audio/video-size status queries, play ranges, volume
and MCI completion notifications are supported. Unsupported editing, capture,
reverse and rate-change requests return errors.

The pinned source profile includes AVI, QuickTime/MP4, MPEG, Matroska, GIF and
FLIC demuxers. Decoders include Cinepak, Microsoft Video 1/RLE, Indeo 2–5,
MJPEG, MPEG-1/2/4, H.264, HEVC, VP8/9, FFV1, GIF, FLIC and raw video. Audio
includes PCM, Microsoft/IMA ADPCM, MPEG audio, AAC, AC-3, Vorbis, Opus and FLAC.
Availability in a system-library build depends on that library's configuration.
An undecodable audio stream fails the open instead of silently losing sound.
Movies are read through 2ine's host filesystem; FFmpeg does not open media URLs.

Frames retain the decoder's YUV420, NV12 or BGRA planes until the PM owner
uploads them. A latest-frame mailbox transfers ownership without copying pixel
data. The SDL renderer reuses a streaming texture, uploads its planes once,
and draws the visible parts of the destination. Other pixel formats use a
cached FFmpeg conversion to BGRA. Stereo audio is resampled to the existing
48 kHz floating-point host mixer. Its sample clock drives video scheduling;
movies without sound use the host monotonic clock. Pause freezes both clocks.
The mixer adds movie sound to DART, MIDI and other audio generators. Module
shutdown removes its generator synchronously after any active callback finishes.

The current path has software decoding, with renderer-assisted color conversion
and display. It does not expose FFmpeg as an OS/2 MMIO video IOProc or codec
DLL, and does not execute IBM video device drivers. Applications calling those
lower-level interfaces directly still need additional compatibility work.
The existing guest DIVE API is separate from this retained-frame path.
Maximum decoded dimensions are 16,384 per axis and 32 million pixels; the MCI
timeline is bounded by its 32-bit millisecond range. Recording, source crops,
DVD/navigation, subtitles, network sources and hardware decoding are unsupported.
HDR tone mapping and wide-gamut output are not implemented.
Playback through a guest application is qualified by synthetic LX programs;
an original commercial movie-player application's full UI is not yet qualified.

## Building

Video is enabled by default. `cmake/Video.cmake` builds FFmpeg **9.0.2** from
the official release, pinned by SHA-256, with network support and automatic
external dependencies disabled. No `ffmpeg` command is run during playback.
An already extracted release can be supplied for an offline build:

```sh
cmake -S . -B build -DTWOINE_FFMPEG_SOURCE_DIR=/path/to/ffmpeg-9.0.2
cmake --build build
```

`-DTWOINE_FFMPEG_PROVIDER=system` uses matching pkg-config libraries:
libavformat ≥60, libavcodec ≥60, libavutil ≥58, libswscale ≥7 and
libswresample ≥4. `-DTWOINE_VIDEO=OFF` omits decoding. The pinned source build
currently uses Unix configure/make tools; a future Windows runtime can use
matching system libraries, but Windows playback has not been built or tested.
The SDL/HAL and decoder interfaces contain no platform-specific window types.

Each video build produces `ffmpeg-build.json` from the **linked libraries**,
including their version, license and configure arguments. Packaging retains
this metadata and uses it in the installed project-source catalogue. The
pinned profile reports LGPL 2.1 or later. System builds can use a different
license: for example, a GPL-enabled Homebrew build reports GPL 3 or later.
The catalogue and bundled FFmpeg license texts preserve that distinction.

The focused regression checks generate original raw/YUV/PCM AVI files and a
compact Cinepak fixture without an installed encoder:

```sh
cmake --build build --target twoine_video_tests lx_loader doscalls
ctest --test-dir build -R '^digital_video' --output-on-failure
```

They check frame content, timestamps, seeking, direct planar uploads, clipping,
ownership release, upload errors, resampling, actual captured stereo output,
and MCI playback through both native calls and checked 32-bit LX bridges.

Per-ball `system.video=false` refuses digital-video device opens with
`MCIERR_DEVICE_NOT_READY`. `system.pcm=false` skips the movie's audio decoder and
shared-mixer registration, reports no movie audio, and schedules retained video
frames against the monotonic clock. MIDI remains independent of the PCM switch.
The shared media switches are described in [ball.md](ball.md).
