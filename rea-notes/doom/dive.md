# DIVE and visible-region compatibility

Owner: `tools/src/2ine-hal`. New host tooling, with no reconstructed-core
milestone claim. The selected Doom executable remains unchanged.

Primary interface evidence is the retained OS/2 Toolkit 4.5 headers
`sdk/os2tk45-master/h/dive.h`, `fourcc.h`, `pmwin.h` and `pmgpi.h`, plus its
`samples/mm/fsdive/fsdive.c` and `samples/mm/dive/show.c`. Imported ordinals are
also present in the existing 2ine `tools/data/os2imports.json` catalogue.
Doom 1.10's published `source110/i_main.c` supplies concrete call sequencing:
UI-thread setup, worker-thread render, LUT8 image buffer, BGR0 palette,
bottom-up copy and `fInvert = TRUE`, source 320x200 by default, scaled screen
destination, copied visible rectangles. This source is evidence rather than
replacement guest code.

The new DIVE module advertises LUT8 input and BGR4 screen output. Unsupported
color formats and non-screen destinations return explicit errors. There is no
direct framebuffer exposure. Image buffers have checked stride/dimensions,
access begin/end state, allocated or caller-associated ownership, and invalid
handle rejection. Portable bindings decode 56-byte caps and 68-byte blitter
structures, pin associated guest storage, allocate page-rounded guest mappings,
and return guest buffer addresses instead of host pointers.

Screen setup captures its PM owner and destination on the UI thread. Worker
blits convert/copy the latest frame into a mutex-protected PM submission and
wake the event loop. The UI owner performs actual host rendering/presentation;
window destruction invalidates captured destinations before freeing them.
Visibility and size changes trigger renewed setup through PM notification.

`WinQueryVisibleRegion` returns region complexity, preserving the SDK ULONG
result. GPI rectangle enumeration orders horizontal and vertical directions
independently, preserving left/right ordering on bottom-up passes. It supports
clipped pages and count-only NULL output, and rejects selected clip regions.
The count-only and leading-edge ordering contracts match IBM's PM reference
(RGNRECT and GpiQueryRegionRects); the corresponding IBM manual is archived at
<https://www.bitsavers.org/pdf/ibm/pc/os2/OS2_3.x/G25H-7191-00_OS2_WARP_V3_Presentation_Manager_Programming_Reference_Volume_2_Oct94.pdf>.

Commands run from workspace root with `PATH=/usr/bin:/bin` to bypass the host's
cmake/linker wrappers:

```
cmake --build tools/src/2ine-hal/build-sdl3-native --target twoine_dive_tests --parallel 6
SDL_VIDEO_DRIVER=dummy SDL_RENDER_DRIVER=software tools/src/2ine-hal/build-sdl3-native/tests/twoine_dive_tests
cmake --build tools/src/2ine-hal/build-sdl3-portable --target twoine_dive_tests --parallel 6
SDL_VIDEO_DRIVER=dummy SDL_RENDER_DRIVER=software tools/src/2ine-hal/build-sdl3-portable/tests/twoine_dive_tests
python3 tools/src/2ine-hal/tests/integration/dive.py --build tools/src/2ine-hal/build-sdl3-native --backend native
python3 tools/src/2ine-hal/tests/integration/dive.py --build tools/src/2ine-hal/build-sdl3-portable --backend unicorn
```

All four pass. Native commands require the workspace's normal unsandboxed
native-worker allowance for i386 TLS/selector calls. The C test verifies actual
read-back pixels after palette conversion, 2x nearest-neighbor scaling,
independent-thread submission, buffer ownership/access errors, invalidation on
window destruction and association release. The generated LX test exercises
actual guest caps and pointers, allocated mappings and associations, retained
contents, stale handles and Unicorn malformed-pointer rejection. The initial
portable fixture caught an unrounded eight-byte map request; the implementation
now rounds allocations to the execution layer's 4-KiB granularity.

`tests/os2libs/test_visibility.c` additionally verifies all four enumeration
directions, clipped/paged/count-only requests, inside/outside/partial counts,
invalid controls, child clipping by parent dimensions, hide/show/resize
notifications, and disabling notifications. A manually compiled portable-host
run passes; integrated CTest results are owned by the main run ledger.

Limits: no direct VRAM, bank switching, other image formats, offscreen DIVE
color conversion, complete host desktop occlusion accounting, or full
Presentation Manager visible-region scheduling is claimed. Actual Doom
behavioral verification remains the parent task's acceptance gate.

Integrated native and portable CTest runs now each pass 3/3:
`ctest --test-dir <build> -R '^(dive_video|dive_guest_apis|pm_visible_regions)$' --output-on-failure`.
The DIVE instance/buffer APIs now serialize ownership, setup and submissions
with a recursive host mutex, independently of Doom's own `hmtxDiveMutex`.
This prevents setup or close from releasing the selected PM target while
another thread is submitting a frame. The PM queue lock never calls back into
DIVE, preserving one-way lock ordering.

The port also imports ordinals 16/17. Its published `source110/fslib.h` declares
`void APIENTRY DiveFullScreenInit(PAPERTURE, PFBINFO)` and
`void APIENTRY DiveFullScreenTerm(void)`. These are notifications after an
external VMAN display-driver mode change, not fallible requests. The software
backend handles the notifications without exposing/rebinding hardware VRAM;
there is deliberately no invented API return code. This does not provide VMAN,
hardware mode switching, or prove the port's hardware fullscreen path works.
