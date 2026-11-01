# Doom host fullscreen

This is a host HAL capability, separate from the original OS/2 VMAN hardware
backend. It does not change an original OS/2 artifact or a reconstruction gate.

## Acceptance and evidence

The host window must enter the desktop display with Alt+Enter and restore its
previous physical position and dimensions. The guest PM window and DIVE render
target retain their logical coordinates and pixels. Mouse events, global pointer
queries and pointer warps must all use that same guest coordinate space. The
toggle Return press/release must not reach the guest; F11 retains its application
meaning. `TWOINE_FULLSCREEN=1` must apply the same fullscreen mode on first show.

Source authority: published Doom 1.10 `i_main.c`, SHA256
`c4ba248d5f15297869e99ad33fbc7cc844dfb13e615fc1325d9ce5a422c51b05`,
and `fullscreen.c`, SHA256
`7e4dc9139aff7d1575456e255d877ce03efb654921cbf554d2028adbefd1c7fd`.
The former queries the frame position, treats mouse coordinates as fixed
`SCREENWIDTH`/`SCREENHEIGHT` coordinates and recenters via `WinSetPointerPos`.
Simply allowing a host fullscreen resize to enlarge PM would change that
coordinate contract. The latter implements the original direct display path;
that path is not supplied by this host change. The source-built LX target remains
`dbbe20fd3fc75f3a32fbfc91e36493dfc6abc33e5ed0bb419b482a0c5955e739`.

The implementation uses the existing SDL 3.4.18 host backend and its actual
fullscreen/window/property protocol, not a guest VMAN success stub. Native i386
and portable host application builds use the existing C HAL ABI. Changes are
limited to HAL video and its contract/integration helpers; original source,
guest messages and Doom's config fullscreen field are not changed.

## Implementation

`hal/sdl/video.c` keeps the render-target size and logical origin separately from
the fullscreen host geometry. It scales the existing texture during presentation,
suppresses host fullscreen resize notifications to PM, transforms local pointer
events, and transforms global pointer reads/warps for the focused fullscreen
window. Alt+Enter down/up is consumed at the host boundary; repeat keydown does
not retrigger the toggle. `TWOINE_FULLSCREEN=0` or unset starts windowed.

The requested initial position is applied after SDL renderer creation: SDL's
software renderer can recreate the underlying X11 window and reset an earlier
placement request. The restored desktop geometry is captured at fullscreen entry.

Bare Xvfb has no window manager. SDL3 accepts an asynchronous fullscreen request
there but no EWMH peer changes the window dimensions. The integration helper
`linux/wm.py` provides a small real X11 window manager on the private test display:
it owns SubstructureRedirect, maps/configures windows, advertises EWMH support,
handles `_NET_WM_STATE_FULLSCREEN`, changes actual X11 window geometry and state,
and restores prior geometry on exit. Normal host desktops supply their own WM.
This helper is test infrastructure and is never started by the runtime.

## Validation

Both commands passed on 2026-11-01:

```sh
ctest --test-dir .state/2ine/doom-finish/build-native -R '^host_fullscreen$' --output-on-failure
ctest --test-dir .state/2ine/doom-finish/build-portable -R '^host_fullscreen$' --output-on-failure
```

Each test takes about 1.25 seconds and runs both shortcut and startup-env cases.
Its real SDL fixture draws two distinct colors into a 160x120 target at (37,53),
checks fullscreen X11 geometry 1024x768 at (0,0), checks scaled screen pixels,
injects a click at physical (768,576) and observes logical (120,90), observes
logical global (157,143), warps to logical global (77,83), restores 160x120 at
(37,53), and compares the exact restored RGB buffer with the initial buffer.
It fails if Return reaches the fixture or F11 ceases to arrive. The fixture also
reads original pixels directly from the retained texture before exit0.

Receipts and captures:
`.state/2ine/doom-finish/build-{native,portable}/fullscreen-results/`.

Verified fixture/library hashes:

| Build | Fixture SHA256 | lib2ine SHA256 |
| --- | --- | --- |
| native | `52554a02a65b5afdbfcf711ee1f055488eb56dd2f7b7f13ab4a8186b785d00d2` | `444e78d4a94933e407885741e2d909eb2cf5bc1c5936c18bbc2fcc1ce74dc1af` |
| portable | `31424a46b9ed18accc74bb0ee44db90e0abde8a0002bcb6198a3b5195fc50d19` | `d0c730a8093c81cc2439fb8bfd1b794e16022f7ecd6f82c5e4b608caca39e3ec` |

Pinned C/H formatting and Python compilation passed for the changed files.
Neighboring `host_media`, `presentation_manager` and `pm_dialogs` contracts also
passed in each build (3/3 native and 3/3 portable).
The fixture proves host behavior on both host ABIs. The main Doom integration
run separately exercises actual guest rendering/input on native and Unicorn;
see the main job/notes for its result. Neither test proves direct VMAN hardware
fullscreen, joystick, CD audio or network multiplayer.
