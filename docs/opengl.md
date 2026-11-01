<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# OS/2 OpenGL with Mesa

The optional `opengl` module implements fixed-width OS/2 desktop OpenGL entries
and PGL context/window integration. Both native i386 and Unicorn use checked
wire callbacks. Guest addresses are never passed directly to host OpenGL.

| Host | Rendering path | Requirements |
|---|---|---|
| Linux | Mesa desktop OpenGL through EGL | Mesa EGL, desktop compatibility contexts and framebuffer objects; libraries matching the worker architecture |
| macOS | Mesa OpenGL → Zink → KosmicKrisp → Metal | Apple Silicon, macOS 26+, Mesa built with EGL/Zink/KosmicKrisp |

OpenGL is opt-in. Ordinary application/runtime builds do not acquire Mesa.
The public interface in `hal/opengl.h` separates context creation, dispatch,
buffer exchange and readback from OS/2 handles, PGL visuals and PM windows.
Other graphics adapters can implement that contract. Vulkan and DirectX adapters
for other platforms are not implemented; requesting an unsupported backend fails
at configuration time. macOS uses KosmicKrisp exclusively and rejects a renderer
that does not identify both Zink and KosmicKrisp. It does not select MoltenVK or
Apple's system OpenGL.

## Linux

Install Mesa EGL development headers/libraries and matching Mesa runtime drivers.
Native Linux uses **i386** libraries, even on x86-64; install their multilib
packages too. EGL development headers are architecture independent.

```sh
cmake -S . -B build-gl -DTWOINE_PORTABLE_LOADER=ON -DTWOINE_OPENGL=ON
cmake --build build-gl --parallel
ctest --test-dir build-gl -R '^opengl_' --output-on-failure
TWOINE_BACKEND=unicorn TWOINE_LIBPATH="$PWD/build-gl" build-gl/lx_loader program.exe
```

For native execution, configure `TWOINE_PORTABLE_LOADER=OFF`. Mesa selects the
Linux renderer. `LIBGL_ALWAYS_SOFTWARE=1` selects its software implementation for
headless verification; the Linux CTest contracts set it explicitly. A custom
Mesa prefix can be supplied through `TWOINE_MESA_ROOT`.

## macOS

Use an SDK and dependencies suitable for Mesa/KosmicKrisp, including LLVM 20.1.8+
with libclc, SPIR-V tools/translator, Meson 1.9.1+, CMake, pkg-config, Mako,
Packaging and PyYAML. See the upstream [KosmicKrisp instructions](https://docs.mesa3d.org/drivers/kosmickrisp.html).
Zink's macOS support is [experimental](https://docs.mesa3d.org/drivers/zink.html#apple-macos-and-moltenvk).

Supply a clean Mesa checkout containing KosmicKrisp and Zink. The helper builds
desktop OpenGL and EGL alongside both drivers, without downloading or updating
the checkout. It records the exact source revision, commands and ICD path.

```sh
python3 tools/build_mesa_macos.py --source /path/to/mesa \
  --build "$PWD/build-mesa" --prefix "$PWD/mesa-install"
cmake -S . -B build-gl-macos -DTWOINE_PORTABLE_LOADER=ON \
  -DTWOINE_OPENGL=ON -DTWOINE_OPENGL_BACKEND=kosmickrisp \
  -DTWOINE_MESA_ROOT="$PWD/mesa-install" \
  -DTWOINE_KOSMICKRISP_ICD=/absolute/path/printed/by/the/helper.json \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
cmake --build build-gl-macos --parallel
ctest --test-dir build-gl-macos -R '^opengl_' --output-on-failure
```

The selected Mesa prefix supplies EGL and its headers, the Zink driver and the
KosmicKrisp Vulkan ICD. The adapter sets `VK_DRIVER_FILES` to that ICD and selects
Zink before initializing Mesa. Those process-wide graphics settings apply to
the worker and its children. A successful build is not a macOS GPU validation;
the three OpenGL tests must run successfully on the target host.

## ABI and supported surface

`TWOINE_OPENGL_ABI=warp` is the default ordinal table used by the supplied IBM
Warp SDK. `TWOINE_OPENGL_ABI=gold11` selects the incompatible ordinal table in
the Open Watcom OpenGL 1.1 import library. Select the profile matching the
application's import library; a process uses one ordinal profile. Function names
are available in both profiles, including name-only core 1.1 additions to the
older table. Ordinal zero is not a callable import.

The generated dispatch covers 336 core GL functions, including immediate mode,
lighting, matrices, display lists, textures, client arrays, pixel transfers,
selection/feedback and evaluators. Driver strings expose the 1.1 surface and an
empty extension list. This is not an OpenGL conformance certification. Color
index, stereo, auxiliary and accumulation visuals are unavailable; visual
selection rejects requirements for them. RGBA visuals use 8-bit components,
24-bit depth and 8-bit stencil, with single or double buffering. Temporary guest
transfers are bounded to 64 MiB; drawables are limited to 16,384 pixels per axis
and 16,777,216 pixels in total.

PGL provides visual enumeration/selection, context creation/sharing, current
bindings, destruction, version/capability queries, synchronization with PM, and
buffer swaps. A context must be unbound before transfer or destruction; a bound
context has one host-thread owner. Host thread exit releases its current binding.
GL rendering uses private framebuffer targets. Front-buffer pixels are copied
through PM's retained raster path, respecting client placement and clipping.
Single-buffer `glFlush`/`glFinish` and double-buffer `pglSwapBuffers` present them.
This readback/presentation step carries a performance cost even with GPU rendering.
The graphics HAL releases SDL's GL binding before Mesa commands and releases
Mesa before SDL rendering. Keep `glBegin`/`glEnd` sequences free of PM drawing.

`pglWaitGL` presents the front buffer and returns a PM presentation space.
`pglWaitPM` releases that space and uploads its pixels into the GL front buffer.
PGL context copying, font display-list helpers and front-bitmap acquisition are
unavailable and return failure. GLU, GLUT and AUX libraries are not implemented
by this module. Applications requiring them need compatible guest libraries and
the other OS/2 services those libraries import.

## Verification

`opengl_mesa` renders and reads pixels using real Mesa, checks front/back exchange,
shared textures/lists and resize state. `opengl_pm` checks retained PM pixels at
a child window's origin. `opengl_guest` runs generated x86 LX programs through
both ordinal profiles; results, executable hashes and logs remain under
`build-gl/opengl-results/guest`. No test substitutes a fake GL renderer.
On Linux, an available `xvfb-run` enables `opengl_desktop_guest` and
`opengl_desktop_pm`, which use SDL's OpenGL renderer and verify the graphics
context handoff. `opengl_macos_policy` checks KosmicKrisp selection and rejection
of other drivers on Linux; it is not a Metal rendering test.

With separately supplied SDK and Watcom fixtures, the SDK's unchanged `SIMPLE.C`
can be compiled and run with a minimal AUX setup adapter:

```sh
python3 tests/integration/opengl_sdk.py --build build-gl --backend unicorn \
  --watcom /path/to/open-watcom --sdk /path/to/os2tk45 \
  --work "$PWD/sdk-opengl-results"
```

The adapter replaces AUX window setup, calls the SDK drawing code and checks
white center pixels and black corner pixels. It does not verify the original
SDK executable or original AUX/TK DLLs. Archive inputs remain unchanged; the
runner stages executable host-tool copies when needed. Linux archived Watcom
host binaries are required by this fixture build command.

Regenerate the GL interface with `python3 tools/gen_opengl.py`; `--check` detects
stale generated files. Generic OS/2 export generation remains independent.
