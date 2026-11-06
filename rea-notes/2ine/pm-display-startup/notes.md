# Main-thread display preparation before GUI DLL initialization

The original Freelance release-0.6 baseline starts a `CorePreloader` worker
before the main thread calls WinInitialize. The worker's resource path is
LoadBitmap → FindResource → WinInitialize → WinCreateMsgQueue → WinBeginPaint →
GpiLoadBitmap. Its complete ordered excerpt is retained with the baseline
receipt in `rea-notes/2ine/lotus-baselines/`. Cursor acquisition precedes this,
but does not itself prove video initialization. Both Freelance and Approach
statically import PMWIN.

SDK `sdk/os2tk45-master/h/exe386.h` defines application mask 0x0300 and
E32PMAPI 0x0300 (PM windowing API), distinct from PM-compatible 0x0200.
The linked SDL source's `include/SDL3/SDL_init.h` requires video initialization
on the main thread and balances subsystem references. These local primary
contracts support preparing the display before a GUI's original DLL
constructors can create background PM users.

`loader/load_lx.c` copies the main LX header and sets GLoaderState.main_module
before loading import dependencies. `loader/modules.c` completes import loading
before initializeModuleGraph invokes guest DLL entry points. The PMWIN native
export initialization hook is therefore a main-thread preparation point for
these GUI imports, without coupling the generic loader to PM or AppKit.

PMWIN now acquires one module-owned HAL display reference after its generated
bridge initialization succeeds, only when the actual main module is LX with
PMAPI application flags. It creates neither a HAB nor message queue, window,
renderer or cursor. Normal WinInitialize keeps its per-thread anchor/reference
behavior. A worker can subsequently request a memory graphics context after
the native display has been prepared on the main thread. PMWIN deinitialization
releases its own reference; worker and independent display-client references
remain separate. A native initialization failure releases the initialized
bridges and returns no native export table with a concrete display error.

VIO/PM-compatible LX, NE, and native-library-only hosts retain lazy behavior.
The existing HAL guard against a first Cocoa initialization from a background
thread and invalid-driver failure remains unchanged. There is no dummy-driver
fallback and no claim that arbitrary background native windows are supported.
A program that dynamically loads PMWIN for the first time on a worker still
receives that guard's error if no display was initialized on the main thread.

`tests/hal/test_pm_display_startup.c` drives real PMWIN bridge/export
initialization against an execution instance and selected main-header modes.
Its GUI/retained paths verify display availability without an eager main HAB
or host window, worker memory HPS/2x2 bitmap scan roundtrip, worker cleanup,
and module reference balance. Other cases exercise PM-compatible/VIO/NE/no
main module and invalid driver. These use SDL dummy/software under the
controlled integration profile; real Cocoa window acceptance is separate.

## Validation

The parent-owned integrated build passed all seven `pm_display_startup_` cases
(gui, retained independent display reference, VIO, PM-compatible, NE, absent
main module, invalid video driver). The first fixture run had a test-only
mistake treating DevCloseDC's returned memory-metafile handle as boolean;
checking its expected null HMF fixed the fixture. Production behavior did not
change in response.

Exact targeted command:

```sh
ctest --test-dir build/runtime --output-on-failure -R '^pm_display_startup_' --parallel 4
```

The parent also ran the real Cocoa driver outside the execution sandbox:

```sh
env SDL_VIDEO_DRIVER=cocoa SDL_RENDER_DRIVER=software SDL_AUDIO_DRIVER=dummy \
  build/runtime/tests/twoine_pm_display_startup_tests \
  build/runtime/libpmwin.so gui
```

It passed the same main bootstrap, worker bitmap scan roundtrip, no-window
and balanced-reference assertions. No separate log file was retained for
these targeted commands; results were reported directly by the parent. The
subsequent full integration run is recorded separately in
`build/lotus/integration-ctest.log`; its outcome is outside this focused receipt.
This is actual no-window Cocoa initialization evidence, not visible Lotus UI
acceptance.
No scratch runtime was built or reconfigured by this worker. The original
Lotus ball and running desktop applications were not changed.
