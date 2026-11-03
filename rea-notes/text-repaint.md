# Word Pro Save Changes text repaint investigation (2026-11-03)

The user reports alternating thin/thick text in the native Save Changes window.
This investigation qualifies font and retained-text rendering separately from
the native presentation and yellow-workspace investigations. It does not claim
the user's flicker is resolved merely because software rendering is stable.

## Actual original caller, private fixture

`build-lotus-visuals/probe-repeat-fonts.py` derives from the visual agent's
original-caller repeat-colors probe. It makes a new APFS clone of the earlier
isolated modal-acceptance ball. It creates a document through the original
Welcome button, types a test character into that private guest document, and
sends the original root WM_CLOSE. Original Word Pro then calls WinMessageBox
with `Untitled1 has changed.  Save changes?`. No document is saved by the probe.
No desktop input is involved. The process is bounded at16seconds with a
20second hard stop; logs are capped at64MiB per stream (actual stderr6.0MiB).

Only diagnostic copies of native controls/messages/paint/color-query units are
compiled into a separate complete runtime. The glyph probe hashes the actual
owned FT_LOAD_RENDER grayscale masks and glyph geometry used by `draw_text`,
after the normal glyph load, rather than simulating a font selection. Its
additional font-metric query occurs before the normal text advance/glyph pass.
The source workspace was360ba3d when this font probe began; the concurrent
display-capabilities change is tracked by the visual agent separately.

Result directory: `build-lotus-visuals/repeat-fonts-r0za1cgq/`.
`native-inventory.json` verifies actual dyld records for PID11183: exactly one
PMWIN, PMGPI, VIOCALLS, PMWINX, PMSPL and lib2ine, with every non-system image
inside this private closure. `result.json` retains native file digests and the
bounded termination result. Original guest executables were not patched.

## Measurements

Original native message-box text control101 painted at origin48,28 in a435×24
client. Its private face was LiberationSans (regular),16pixel Em, bold0,
italic0, ascent15, descent4, average width9. Baseline15 and final pen334 remained
unchanged. The PS uses charset0/font-index0/charbox0,0.

All37 actual glyph geometry/coverage hashes were
`9326a1d2f16c960b` (64-bit FNV diagnostic hash) across initial painting, forced
full invalidation and two successive left/right half invalidations. Half
invalidations specifically test clearing/alpha compositing at glyph boundaries.
The grayscale provider uses integer placement, copies coverage into owned
buffers and keeps its FreeType face private to each font object; no fractional
glyph transform or shared mutable glyph cache exists in this path.

Four full495×104 modal retained-RGB frames, stages181/201/221/231, compare
byte-for-byte equal. Full-frame SHA256:
`272f8d03ecf32e60b0b3154460542abe17fb9bb571b80a71a64b9f3540e6d6ca`.
Text-body SHA256:
`e32a72f26ac62660b43d046ab1b85b56118dcd878f4e28ce4c0be6443957863d`.
These comparisons use the raw RGB bytes as well as hashes. See
`font-evidence.json`, bounded stderr `FONT DRAW`/`FONT MASK` records and the
corresponding PPM/PNG captures. The actual raster was visually inspected.

The original probe also exercises File/Edit menus and document deactivation/
reactivation before opening the modal. It does not claim to reproduce native
modal focus/window-compositor changes: SDL dummy/software is used throughout.

## Focused persistent regression

`tests/os2libs/test_help_fonts.c` now checks grayscale masks and geometry across
all12 bundled family/style slots at11/16/24pixels. Repeated metrics, unrelated
glyph loads, advances and kerning queries must leave subsequent glyph coverage
byte-identical. The existing physical-face/style/character-box PS transitions
still run in the same test.

The updated test was compiled as `Runtime/font-mask-regression` inside the
private complete closure, with all non-system dependency references relocated
to `@loader_path`. It was ad-hoc signed with runtime entitlements and run using
that closure's fonts/modules. This avoids rebuilding shared runtime source
during the independent alias-fixup work. It passed; log
`/tmp/2ine-font-mask-regression.log`. No production font/text source change was
justified by these results. The normal coherent build should incorporate this
test-only update in its final suite.

## Hidden native Metal probe handoff

The earlier native target/resize diagnostic remains available:
`build-lotus-visuals/paint-diagnostic/retina-probe.c` and its complete old private
`RetinaRuntime/retina-probe` executable. `retina-video.c` is a diagnostic HAL copy
that adds SDL_WINDOW_HIGH_PIXEL_DENSITY and bounded renderer-state logging.
`retina-metal-2x.log` records actual Cocoa backing density2 and the retained
510×294 target/viewport. It creates a hidden335×164 window, resizes510×294,
draws a10pixel blue marker and checks pixels across two presentations.

Run the old executable from its own RetinaRuntime with SDL_VIDEO_DRIVER=cocoa,
SDL_RENDER_DRIVER=metal, TWOINE_DIAG_RENDERTARGET=1 and DYLD/LD_LIBRARY_PATH
pointing at that same closure. For a current-runtime build, compile the small
C probe against a fresh complete private closure (`-Ihal/include`, its
lib2ine.dylib and `-Wl,-rpath,@executable_path`), keep the executable inside that
closure, relocate/audit dependencies and sign it. Never overlay the old custom
retina-lib2ine into a current mixed module set.

That earlier marker test is not native Save Changes font-flicker acceptance.
The remaining gate is simultaneous native-frame/retained-canvas measurement
during the reported thin/thick transition, without a readback perturbing the
presentation schedule. Root owns the native presentation investigation.
