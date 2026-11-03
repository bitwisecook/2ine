# Native acceleration behind the OS/2 multimedia interfaces

This is a proposed architecture, not an implementation or qualification claim.
The current API inventory and its source/SDK references are in
[multimedia-gaps.md](multimedia-gaps.md). Preserve the guest ABI and observable
behavior while moving expensive work into shared modern host engines. Adding a
fast backend does not make an absent MMIO, SSM or RTMIDI entry point implemented.

## Shared engines and frontends

| OS/2 frontend | Shared host engine | Current implementation | Proposed acceleration |
| --- | --- | --- | --- |
| MCI digitalvideo; future MMIO video IOProcs/codecs and SSM video handlers | Decoder/demuxer, media clock and retained frame queue | FFmpeg software decode; bounded owned YUV420/NV12/BGRA frames; SDL streaming textures; audio-clock scheduling | FFmpeg hardware-device/frame adapters, with host-native frame leases and explicit software fallback |
| DIVE; future HWVIDEO; decoded video display | Frame import, palette/conversion/scaling and composition | DIVE LUT8 software conversion; movie streaming texture uploads | Shared GPU texture upload/import, shader palette/YUV conversion and scaling; damage/clip-aware composition |
| PGL/OpenGL | Mesa contexts and shared composition | Mesa/EGL; macOS Zink/KosmicKrisp/Metal policy; PM readback path | Retained GPU surfaces and synchronized texture sharing to remove full-frame readback where supported |
| GPI bitmaps, ROPs, text and metafiles | Retained PM renderer and raster operations | SDL retained canvases with software pixel paths for several operations | GPU paths only where exact palette/ROP/clip behavior is preserved; CPU fallback for unsupported operations |
| MCI waveform/sequencer, DART, future SSM audio | PCM queue/resampler/mixer and host device | Shared floating-point stereo mixer; DART/MIDI/movie generators | Native host audio device adapters, bounded low-latency queues, capture separately implemented and advertised |
| MIDI files; future RTMIDI ports/nodes | Timestamped MIDI routing and synthesizer/host-port service | Sampled GM and optional Munt synthesis | Host MIDI port adapters plus existing synthesis, with explicit device selection and guest timing semantics |
| Gameport/joystick APIs or evidenced device IOCTLs | Controller enumeration and input event service | Keyboard/mouse/relative capture; no general controller bridge | SDL/native controller adapter, stable guest device identity, axis/button calibration, hotplug and focus policy |

The engines belong behind HAL/runtime interfaces. MMPM and other OS/2 modules
translate their own handles, flags, errors and callbacks; they should not each
grow independent FFmpeg, Metal, audio-device or controller implementations.
User-supplied original DLLs remain a separate provider choice. Their user-mode
logic can be useful, but kernel/display/audio driver dependencies cannot be
declared satisfied by the existence of a host engine.

## Frame ownership and presentation

Extend the retained-frame contract with an opaque native-surface lease, format,
dimensions, visible crop, color range/matrix and timestamps. The producer owns
the decoder frame until ownership transfers; the presentation owner releases it
only after upload/import and any GPU fence complete. Keep a bounded latest-frame
queue and discard obsolete video frames without discarding scheduled audio.
Never expose a host texture/pointer as a guest address.

Proposed platform adapters are Metal/CVPixelBuffer on macOS, VAAPI with supported
DMABUF import on Linux, and D3D11 decoded surfaces on Windows. These are design
targets, not paths presently tested in 2ine. Probe the actual decoder, format,
renderer and import compatibility at runtime. Hardware decode that immediately
copies through CPU memory may still help decoding but must not be described as
zero-copy. Unsupported combinations use a declared software decoder/upload path
or fail clearly if the user explicitly requires unavailable acceleration.

DIVE's guest buffers must retain their documented writable/readable lifetime.
Upload on a completed guest access/blit rather than allowing a GPU to race guest
writes. Indexed data can remain indexed on upload with a separately updated
palette texture. Respect source/destination rectangles, strides, clipping and
orientation. Video color conversion and scaling may share machinery with DIVE,
but OS/2 RGB/ROP results must remain exact where the API requires them. PM damage,
nested paint targets, native window scaling and host composition must preserve
the same retained content after exposure or resize.

## Audio, clocks and callbacks

Keep one device/mixer service, not a new host stream for each MCI or DART handle.
The audio sample clock remains the master for audiovisual playback; monotonic
time drives video-only playback. Seeking and rate/format changes establish a new
clock epoch and invalidate queued old frames/samples. Preserve pause, completion,
notification ordering and MCI_WAIT behavior.

Host realtime callbacks consume preallocated queues without guest execution,
blocking I/O or unbounded allocation. Marshal OS/2 callbacks onto the appropriate
guest execution context. Stop/unload unregisters synchronously after in-flight
callbacks finish, as the current shared-generator lease already requires.
RTMIDI adds timestamped ports/routing without conflating MIDI master state with
PCM master state. External MIDI output and capture require separate permission,
device-loss and unavailable-device behavior appropriate to each host.

## Delivery phases and acceptance

1. Preserve the current software paths and establish original-application
   playback/DIVE evidence, timestamps, content and teardown tests. Complete the
   evidenced MMIO public contracts using the common decoder/file engine; do not
   substitute success-only IOProc or codec exports.
2. Add GPU DIVE conversion/scaling and a shared compositor import interface.
   Verify indexed palette changes, partial updates, clipping, resize/exposure,
   overlapping PM children and buffer access lifetimes against software output.
3. Add one hardware decoder adapter at a time, beginning with the supported
   primary host. Test software/hardware output tolerance, seek/pause/stop,
   decoder fallback, device loss, frame leases, bounded memory and audio drift.
   Measure decode time, CPU copies, upload bytes, dropped frames and latency;
   distinguish actual acceleration from a merely successful API call.
4. Remove the OpenGL-to-PM readback where compatible shared-surface/fence paths
   are proven. Keep Mesa as the default guest provider and software fallback
   explicit. Extending guest GL/PGL coverage is a separate compatibility task.
5. Expand wave/sequencer discovery, native MIDI ports, controllers and recording
   according to original application calls. Add SSM/HWVIDEO frontends when their
   real contracts and clients justify them, reusing the engines above.

Every platform must advertise the implementation actually available on that
host. Saved per-ball choices stay portable; receiving-host availability and
fallback are resolved without rewriting the requested preference. Existing
video/PCM/MIDI/DIVE/OpenGL controls remain the master gates. Windows remains a
design target until its build and original-guest execution are qualified.
