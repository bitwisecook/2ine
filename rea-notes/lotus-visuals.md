# Lotus frame and native-control rendering

## Scope and authority

User reports on 2026-11-03: installer dialog chrome missing over background,
long free-space text over the drive combo's arrow, Word Pro background tint
flashing during menu/UI navigation, and default-handler logs for messages 77,
112 and 4099. Original OS/2 reconstruction milestones are unchanged.

The selected SDK is `/Users/jimd/src/os2/sdk/os2tk45-master/h/pmwin.h`:
FCF_TITLEBAR/SIZEBORDER/DLGBORDER/BORDER are 1/8/0x100/0x200; corresponding
FS border styles are 0x800/0x80/0x100. FRAMECDATA is 10 bytes, two-byte packed,
and its resource-module field is genuinely USHORT. PP foreground/background
RGB values use ids 1/3; their indexed alternatives are 2/4. WinQueryPresParam
converts the indexed alternative with QPF_ID2COLORINDEX (4).

IBM PM Programming Reference Volume II describes WM_QUERYBORDERSIZE's POINTL
output, success result, and use of SV_CX/CYSIZEBORDER, SV_CX/CYDLGFRAME and
SV_CX/CYBORDER:
https://www.bitsavers.org/pdf/ibm/pc/os2/OS2_3.x/G25H-7191-00_OS2_WARP_V3_Presentation_Manager_Programming_Reference_Volume_2_Oct94.pdf
The ordinary default window procedure returns FALSE; the frame procedure must
supply the border. Message 112 is WM_MOUSEMOVE; 4099 is WM_USER+3.

## Implementation

WC_FRAME previously discarded FRAMECDATA and performed no frame border paint.
It now retains create flags, translates the frame border styles, reports checked
WM_QUERYBORDERSIZE output, reports frame identity, and shares border metrics
between frame/client rectangle conversion, client/menu/title positioning and
border rendering. WinDefDlgProc draws the frame border after its background.
WinCreateFrameControls906 has a checked 10-byte guest bridge and creates a real
WC_TITLEBAR, system menu, resource menu and accelerator table. Captions update
both host title and guest title control. FRAMECDATA low16 module aliases expand
to a leased live module handle before menu/accelerator resource queries; this
was required by the original HELPMGR agent's observed hmod 65593 -> USHORT57.

Native combo parents no longer duplicate the selected text across the entire
control. The edit child owns the text, and a dedicated drop button/arrow owns
the right-hand square. WinSetWindowText now synchronizes the edit child.
Native glyphs respect the HPS paint/visible region, rather than clipping only
to ancestor bounds. Native controls and the default dialog background honor
owner-inherited presentation colors, and changes invalidate affected native
controls. This repairs a reproducible mismatch between custom owner colors
and hard-coded grey native repainting; it is not proof that every Word Pro
flashing path is resolved.

Expected default/private messages no longer print unsolicited stderr errors;
they remain trace events. WM_QUERYBORDERSIZE is implemented rather than hidden.

## Validation

ARM64 CMake runtime build passes after other concurrent agents' intermediate
link errors were resolved. Five focused checks pass in
`/tmp/2ine-visual-tests-final.log`: presentation_manager, pm_dialogs,
open32_apis, pm_visible_regions and pm_thread_posts. New PM contracts check
border query guard bytes, rectangle round-trip, title updates, border pixels
without erasing client pixels, unchanged drop-button pixels with long combo
text, and inherited/replaced background RGB. One initial test failed because
it captured the combo before explicitly painting it; corrected test paints the
combo and edit before each snapshot. Pinned clang-format21.1.8 was applied to
owned C units. Root integration owns final whole-tree generator/style/tests.

Ignored probe `build-lotus-visuals/probe-wordpro.py` copies the real installed
ball to `build-lotus-visuals/WordPro.2ineball` (ignoring logs/ipc), asks the C
engine for a launch plan, clears inherited reserved environment keys BEFORE
merging that plan, then runs headless for 15 seconds. No real ball files were
modified and no UI/licence choices were made. An earlier run cleared those
keys in the wrong order and failed to resolve C:; it is not an emulator defect.
The corrected run logged 405,403 stderr bytes, reached the original Welcome to
Lotus Word Pro dialog, and exited 0 after our 15-second termination. Its result
is `wordpro-result.json`, trace `wordpro.stderr`, and plan `launch-plan.json` in
the ignored probe directory. Original and copied WORDPRO.EXE SHA-256 are both
bf122341b274d7bee0909b66ebd6f5bc884d407828105e532761eb405e0e4387.

The real ball already contains COMPNENT/SPELL/LTSPLO04.DLL, ltspho04.dll,
ltspfo04.dll and lotusen2.dic; the install log confirms their copy. The bounded
probe queries wPANSpellRequest and registry Software\\Lotus\\Components\\Spell\\4.0,
but remains at Welcome and does not reproduce the later missing dictionary
dialog. Root must continue that investigation beyond Welcome in an isolated
ball. Do not infer that the spelling module is missing from disk.

## Limits and next action

No actual original installer screenshot comparison or interactive Word Pro
menu flicker qualification has been completed in this agent. Retained frame
flags do not yet create min/max button or scrollbar controls in
WinCreateFrameControls; the main implemented subset is title/system/menu/accel
and border geometry. Creation/drawing of those remaining requested controls
must not be claimed as complete. WM_FORMATFRAME currently reformats known
controls directly, rather than filling a caller-provided SWP array for arbitrary
frame subclass extension. The original help agent has been informed.

Root requested handoff to free this agent slot for video work. Continue with
original screenshot/runtime acceptance, spelling-path/DLL initialization,
frame-subclass formatting where required, and the full integrated test run.

## Welcome geometry and titlebar subclass state (2026-11-03, later work)

The user supplied a screenshot at 14:31 showing the native Welcome dialog's guest
teal title painted over its top control text and bottom buttons nearly clipped.
This belongs to Word Pro; the surrounding black window is not the Lotus installer
and does not establish any missing installer-background bitmap.

Bounded startup evidence is retained in
`build-lotus-visuals/welcome-layout-startup.log` (first 2 MiB of the original visible
probe trace, before the root probe log was reused). Original WinLoadDlg loads
resource 3452 from module handle 65686. Root template 335×164 dialog units becomes
502×266 pels after its font presentation parameter. The original top control is
created at y 243, height 17, while our title occupies y 242..262. The loader had
allocated that control layout as the entire frame and placed direct children at
frame origin, giving the title/borders the same space as controls.

`dialog_template.c` now computes non-client extents through WinCalcFrameRect,
reserves them around the converted template control layout, and positions
immediate template children at the resulting content origin. Nested controls
retain their own parent-relative positions. Geometry uses existing DPI/font and
frame metrics; there are no application-specific coordinates or background fills.
The interpretation is driven by the original layout and the generic requirement
that template controls fit the advertised layout; broad SDK descriptions call the
root a frame but do not explicitly resolve this extent distinction. Win32 client
sizing documentation was consulted for comparison and is not claimed as OS/2
ABI authority. Native regressions assert usable layout dimensions, top-control
separation from the caption and bottom-control clearance at 96 and 192 DPI.

A separate concrete defect explains the black/stalled Word Pro startup paint
path. Word Pro subclasses its FID_TITLEBAR, then stores pointer 539982208 with
WinSetWindowULong(titlebar,0,...). Our WC_TITLEBAR definition reserved zero extra
bytes, so that store failed. The titlebar's original subclass repeatedly queried
zero and returned without painting; the trace devolved into millions of
WinQueryWindowULong(titlebar,0)/WinGetMsg/WinDispatchMsg calls, starving the client
window's pending paint. This is an observed failed contract, not a choice of
background color. WC_TITLEBAR now reserves four bytes for the observed user word,
like the existing standard controls. A native regression round-trips the word.

WinSubclassWindow also lacked a portable guest bridge for frameControlProcedure.
It would have returned a truncated host procedure pointer once the original
subclass reached its forwarding call. A checked stack32 bridge now returns a
real executable guest address and is unmapped with the PM anchor. The generated
LX test `tests/integration/titlebar.py` stores titlebar userdata, installs a real
guest subclass, reads that data during WM_PAINT and calls its returned previous
native procedure successfully. No private original procedure bytes were changed.

The full runtime build completed and focused 11/11 tests passed at
`/tmp/2ine-welcome-focused-tests.log`: PM/dialog/frame, generated LX titlebar,
Open32, visibility, cross-thread posts and four display DPIs. The first build
attempt failed because the new test referenced hidden internal functions;
the test was corrected to exercise public WinCalcFrameRect. Rebuilding every
AnchorBlock-dependent module was required; interim tests against mixed old/new
objects are not implementation failures or acceptance evidence. Root and the
video agent coordinate immutable complete runtime snapshots before a new visible
probe. Final application rendering remains to be recorded after that probe;
these isolated API tests do not by themselves qualify the whole Word Pro UI.

The subsequent full coherent runtime suite passed **61/61** in 25.63 seconds
(`/tmp/2ine-welcome-integrated-tests.log`). A fresh isolated APFS-cloned ball
(`build-lotus-visuals/WordPro-layout.2ineball`) launched the original application
for a bounded ten-second dummy-renderer probe. Results are retained in
`build-lotus-visuals/layout-wordpro-result.json` and its 4,692,388-byte stderr
trace; the process exited cleanly after the requested bounded stop. In this
trace the original main titlebar (HWND 289013773) queries its user word only
twice and executes WinBeginPaint once. The original main client (289013792)
also reaches WinBeginPaint, confirming that the observed starvation has ended.
Welcome now receives outer geometry 510×294; its original top custom control
is y 247..264, below the guest title at y 270..290. This is original application
behavioral evidence, not a screenshot or a claim about final colors.

The video agent froze and hash-checked the complete latest runtime at
`build-lotus-visuals/coherent-probe-inputs/20261103-155513-final`, including
the root's independent PMSPL driver-data fix. Visible Welcome/main-client
appearance and the path beyond Plain Document remain the root's acceptance
work; this agent has not interacted with that active probe. Earlier limits
in this note about direct-only WM_FORMATFRAME formatting were superseded by
the separately tested SWP-array implementation recorded in
`rea-notes/original-help.md`. Full application behavior, all frame controls,
all help content and physical printing remain broader acceptance questions.

## Retained repaint and first presentation (2026-11-03, followup)

The user's exact reproduction was switching Welcome's Open Existing / Create
SmartMaster pages: children disappeared until Cmd-Tab away/back. The original
application was driven in an isolated dummy-renderer process with synthetic
HAL button events to its real LtsPanelTab. Scratch-only code captured retained
canvas pixels and bounded native/event logs; no desktop input or license action
was performed. The injection objects and libraries remain under ignored
`build-lotus-visuals/paint-diagnostic/`, not in production source.

Three independent retained-canvas contracts were repaired:

* Word Pro clears Welcome's WS_VISIBLE using QWL_STYLE, changes child
  visibility, then restores the style. WinInvalidateRect retained damage while
  the ancestor was hidden but did not schedule a paint. Restoring visible style
  now schedules existing dirty descendants; it manufactures no damage and does
  not change the separate WinEnableWindowUpdate behavior.
* Damage is represented by one conservative bounding rectangle. Combining
  disjoint parent damage expanded the parent paint into a clean gap without
  marking children in the gap. Child propagation now uses the stored bounding
  update area.
* Lower siblings can overwrite a clean transparent upper frame in the shared
  canvas. Non-sibling-clipped damage now schedules affected upper siblings for
  restoration. Propagation is restricted to lightweight siblings and changed
  damage/new scheduling, avoiding repeated propagation of identical regions.

Native pixel regressions in test_visibility.c exercise both page transitions,
exposure comparison, disjoint bounding damage and overlapping transparent
frames. The transparent-frame test failed before its repair at line117 in
`/tmp/2ine-overlap-before.log` and passed afterward. test_dialogs.c now sends
actual HAL pointer down/up at a rendered dialog button's center and observes
WM_COMMAND at 96 and 192 DPI, including frame insets. This does not establish
CUA's screenshot-coordinate conversion for every window.

Early original-pixel claims from a diagnostic library injected into a normal
prepare-launch cache were withdrawn: DYLD inventory found two PMWIN and two
VIOCALLS loads. They are not final acceptance. Subsequent private runtime
closures were fully relocated to @loader_path, audited, and observed with
exactly one each of PMWIN, PMGPI, VIOCALLS, PMWINX, PMSPL and lib2ine, with no
non-system libraries outside the selected closure. Native closure qualification
is a prerequisite for interpreting application state.

After the repaint repairs the original application's title, labels and list
survived both tab transitions. A remaining 204 frame-edge pixels still differed
from a full exposure in one coherent original run; whole-buffer tab equality
is therefore not claimed. Exact startup canvas before/after exposure became
equal after the software-surface repair below. Original preview drawing remains
a separate investigation; an empty right pane does not establish missing files.

SDL's software renderer overrides its active surface with the window surface
when the native window surface becomes invalid. Our resize selected a new
canvas first and invalidated the native surface afterward, so subsequent paint
went to the native backbuffer while the retained texture stayed transparent.
A bounded original probe observed white/opaque canvas pixels immediately before
its first presentation and black/transparent immediately afterward, despite
successful drawing/presentation. Native size/sync now precedes canvas creation;
the software default surface is activated/flushed before restoring the canvas
after native resize, show, border or fullscreen changes. Grow/shrink, repeated
presentation and coordinate-preservation regressions cover the software path.
SDL dummy does not support native border changes; that limitation is reported
rather than asserted as a successful native border operation.

The initial implementation also warmed up hardware renderers. The user then
reported a 1.52x enlargement/clipping inside Welcome's unchanged native window.
Actual Metal and headless retained BMPs both contained the normal 510x294 canvas.
The root font was 8.Helv, DLU factors6x13, selected font11px and display96dpi in
both; this was not a configured OS/2 DPI change. A hidden real Cocoa/Metal
high-density probe verified target scale1x1, viewport510x294 and native pixel
size1020x588, preserving a10px mark through repeated presents.

The software workaround is now restricted to SDL's software renderer. SDL3.4.18
Metal source acquires nextDrawable on the first default-target draw and retains
it until presentation, while GetOutputSize independently reads current layer
size. The original construction calls native positioning at335x164 before the
template resize to510x294. A warmup can therefore hold an unpresented335x164
drawable across that resize, explaining the observed1.52 ratio despite correct
canvas/viewport metadata. Hardware renderers now keep their texture target and
acquire their drawable only during final presentation. This cause is supported
by source and the actual layout evidence; final first-window visual acceptance
of the software-only guard is assigned to the root. No forced font scaling or
application-specific resizing was added.

Primary SDL references: [software surface activation](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/render/software/SDL_render_sw.c),
[Metal drawable acquisition and output dimensions](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/render/metal/SDL_render_metal.m).

The software-only guard build is complete; the preceding integrated root run
passed64/64 tests. Focused renderer/PM/frame/DPI/input checks passed12/12 before
the guard; its focused rerun is recorded separately in
`/tmp/2ine-software-only-tests.log`. The root-created visible acceptance app is
not silently replaced. Diagnostic and acceptance generations have unique bundle
IDs, private balls and audited closures; their generated mountpoint paths,
including encoded media-device paths, are explicitly rewritten for each clone.

## Lotus caption provenance (separate from DPI)

Scratch system-color instrumentation identified the original caption callback
0x20090280 as LTCAPO10.DLL object0, base0x20090000, offset0x280. The separately
supplied DLL SHA256 is
`9a9bab91b17ab9144980f34094a0b52fc5a98c176f9534dc3e5f1d793e2ae9e3`.
`build-lotus-visuals/title-provenance.json` retains the exact mapping and paint
call sequence. While the main owner is disabled by modal Welcome, it queries
SYSCLR_ACTIVETITLETEXT(-28), ACTIVETITLETEXTBGND(-30), INACTIVETITLETEXT(-29),
INACTIVETITLETEXTBGND(-31), then fills black, selects a bold logical font and
draws white text, followed by yellow/white fills. The lack of WinQuerySysColor
lines in older uninstrumented traces was not proof of absent queries.

This is Lotus's custom caption DLL, separate from the native default teal
frame-control procedure. Gradient/palette/bitmap paths require further original
GPI investigation; no native overpaint or hardcoded Lotus gradient was added.

Final root acceptance of the software-only guard: the fresh immutable
`Word Pro DPI Acceptance.app` opened its first native Welcome at1020x652
screenshot pixels, with normal text and every bottom button fitting, without
resize or focus cycling. Its actual dyld inventory has exactly one copy of
each of the six key native modules and no non-system image outside its Runtime;
see `build-lotus-visuals/dpi-acceptance/native-inventory.json`. Guard-focused
checks passed12/12 in17.94s (`/tmp/2ine-software-only-tests.log`). That accepted
runtime is preserved; later frame controls, bitmap-preview APIs and menus need
a subsequent complete snapshot and their own acceptance. The source renderer
is released to integration. The clipping cause was early hardware drawable
acquisition by the software workaround, not an OS/2 DPI or font-size setting.

## Stock frame controls and title-bar modes (2026-11-03)

User reports identified missing/grey title controls, a system item rendered over
the action bar, and native decorations around PM popup menus. Source inspection
established two concrete defects: createSystemMenu deliberately withheld
WS_VISIBLE on desktop frames while frame layout still reserved its slot; and
WinCreateFrameControls never created FID_MINMAX. The original application UI
still requires a fresh coherent snapshot to qualify final appearance.

The primary IBM PM reference (Volume II, October1994, chapters13/15) and selected
Toolkit4.5 `h/pmwin.h` establish real control handles, frame formatting ownership,
MS_ACTIONBAR/MS_TITLEBUTTON and stock SC_* commands. FID_MINMAX is a WC_MENU
title-button control, not WC_MLE or an invented stock class. MS_TITLEBUTTON
selects title colors; it does not replace arbitrary text with a system glyph.
The primary reference is
[IBM PM Programming Reference Volume II](https://www.bitsavers.org/pdf/ibm/pc/os2/OS2_3.x/G25H-7191-00_OS2_WARP_V3_Presentation_Manager_Programming_Reference_Volume_2_Oct94.pdf).
Toolkit metrics33/34 are SV_CX/CYMINMAXBUTTON;31/32 are slider lengths, not
system-button dimensions. The system menu retains its title-height square;
min/max widths use metric33.

Stock system buttons are visible with guest titlebars; FID_MINMAX now has real
SC_MINIMIZE/SC_MAXIMIZE items and changes its maximize glyph/action to Restore
for a maximized frame. Generic title-button text is preserved. Stock glyph
buttons keep their raised face; ordinary title controls use actual active and
inactive SYSCLR values. A merged document system popup tracks its active action
bar for focus/dismissal while retaining its original frame command owner.

Native/OS2/Both applies to mapped top-level standard frames. Native suppresses
guest border/title/system/minmax presentation, hit targets and reserved space;
OS2 suppresses the native host border; Both preserves both. All actual standard
handles and WM_QUERYFRAMECTLCOUNT/WM_FORMATFRAME records remain. Suppressed
controls report SWP_HIDE, cannot be forced visible with WinShowWindow, and retain
their requested visibility for later embedding. Embedded frames retain guest
controls. Newly created and reparented top-level WC_MENU windows both receive
borderless host presentation.

Default WM_SYSCOMMAND handles min/max/restore/close and mouse Move/Size tracking;
close posts WM_CLOSE so an application can handle it. Title dragging maps local
mouse positions to the frame parent's coordinates and uses PM mouse capture.
Host mouse dispatch previously ignored the capture handle; it now routes motion
and release to that window, including outside its bounds. SDL click counts reach
PM double-click messages, enabling title maximize/restore, system-button close,
and embedded minimized-frame restore. Native minimize/restore events reconcile
guest state when the user restores through Dock/taskbar. The narrow SDL wrapper
flushes before state changes and preserves the software-only surface activation
guard. Embedded minimized frames use icon metrics and a title fallback because
native WM_SETICON pointer artwork is not yet implemented.

Focused acceptance: all12 PM/frame/menu/dialog/visibility/DPI/guest-titlebar and
runtime-feature checks passed in `/tmp/2ine-frame-chrome-tests.log`. Expanded
`test_menu_chrome.c` covers all modes, unique child-owned SWP records and query
purity, suppressed visibility and client geometry, reparenting, real minmax
commands/restore geometry, merged system-popup routing/dismissal, custom title
text, capture across title/client boundaries through the host event dispatcher,
and native/embedded minimized restoration. Complete build log is
`/tmp/2ine-frame-chrome-all-build.log`. Internal Window and LxHostEvent layouts
changed; acceptance requires a complete rebuild and a coherent runtime snapshot.

Remaining limits: no new live Lotus/menu screenshot or Linux/Windows host
acceptance in this agent. Full keyboard tracking, WM_QUERYTRACKINFO constraints,
all border sizing hit areas, custom menu bitmap/owner-draw items and original
application icons are separate compatibility gaps. Mouse tracking supports
Move and bottom-right Size; it is not a claim of the complete OS/2 tracking API.

The completed integrated runtime suite passes **66/66** in26.09seconds, recorded
in `/tmp/2ine-frame-chrome-integrated-tests-final.log`. An initial expanded run
found activation color invalidation had bypassed the existing frame-to-client
WM_ACTIVATE forwarding; forwarding was restored and the thread activation
regression then passed. The concurrent modal test was rebuilt after its
intermediate undeclared WinValidateRect call was removed by its owning agent.
The final suite includes that modal repaint correction and actual bitmap tests.

## Fixed original dialogs and native resize affordances

Root's later ModalAcceptance showed Welcome at561×426 after possible user
resizing, with unchanged original children at the lower left and a large blank
upper region. This was not established as a startup/DPI regression. The retained
actual original trace `coherent-tabs-wordpro.stderr:5685` proves Welcome's frame
style940002a0 and510×294 geometry: FS_DLGBORDER is present, FS_SIZEBORDER0x800 is
absent. Toolkit4.5 `h/pmwin.h:2735` defines that sizing-border bit. The HAL had
created every host window with SDL_WINDOW_RESIZABLE regardless of PM frame style.

Mapped standard frames now request native resizability only with current
FS_SIZEBORDER. Creation, reparenting and changes to that style bit reapply the
policy; transient visibility style changes do not reconfigure the host. Top-level
menus are fixed. Unsolicited native resize events are rejected/restored for fixed
frames, while guest WinSetWindowPos(SWP_SIZE) remains permitted. This preserves
original fixed-dialog semantics instead of manufacturing responsive child
coordinates. Native request changes flush queued drawing and retain the accepted
software-only surface guard.

`/tmp/2ine-fixed-dialog-build.log` records a successful complete rebuild. The
expanded native regression injects the observed561×426 resize into a fixed
240×160 frame, verifies unchanged guest geometry, verifies programmatic resize,
then enables FS_SIZEBORDER and verifies host-event resize and dynamic disable.
SDLdummy explicitly does not support native SetWindowResizable; native property
assertions are conditional on host support, while PM policy assertions always
execute. The initial complete suite passed65other tests and exposed this test's
unsupported dummy-host assumption. After correcting that assertion, focused
frame/menu/dialog/thread checks pass4/4. Root owns a fresh coherent native-host
acceptance; no new application screenshot is claimed here.

## Nested modal presentation (2026-11-03, after DPI acceptance)

After Return from the selected Welcome template, the root observed a live but
entirely black Untitled1 modal,1484x616 screenshot pixels. Focus/exposure did
not reveal it. The read-only curated log identifies native WinMessageBox's
generated template: root330x76 DLU, three text lines, one OK button and icon,
with normal96dpi metrics. It does not contain the original message text or
prove the original call stack. The user-visible message is not yet identified.

A concrete native contract defect was found independently of surface resizing:
presentWindow suppressed every host window whenever any WM_PAINT callback on
the anchor was active. A document paint callback can synchronously enter a
modal dialog loop; that outer callback stays active until the dialog closes,
so the separate modal's canvas was never presented. A native regression in
test_dialogs.c opens an owned modal inside WM_PAINT, paints its visible
controls and requests presentation through public GetPS/ReleasePS. It failed
on modal->last_present with the old global guard
(`/tmp/2ine-paint-modal-before.log`).

Presentation now consults a thread-local stack of active paint window handles
and anchor pointers. Only a matching live window with the same heavy canvas
defers presentation. Window lookup after callbacks handles destruction, and
no Window or AnchorBlock layout field was added. The regression also asserts
that the outer canvas remains unpresented throughout its paint callback and
can present after that callback returns. Focused PM/dialog/frame/visibility/
threading/Open32 tests passed8/8 in3.28s
(`/tmp/2ine-paint-modal-after-tests.log`). An intermediate test called an
unexported private helper and then an unavailable WinValidateRect; both were
removed before this failing/passing proof. It now uses only existing public
paint/PS APIs for its actions.

The original Untitled1 modal remains the next acceptance gate; this native
regression alone does not establish that its message is the spelling error or
that the original call entered through WM_PAINT. The live accepted DPI app
was left unchanged for comparison. A subsequent coherent build is required
because the concurrent frame-chrome work changed shared Window/LxHostEvent
layouts, independently of this paint-stack change.

## Original Word Pro caption colors and bounded menu repaint, 2026-11-03

The final diagnostic uses the original installed Word Pro and LTCAPO10.DLL in a
new private clone of `build-lotus-visuals/modal-acceptance/WordPro.2ineball`.
`build-lotus-visuals/probe-actual-colors.py` builds diagnostic copies of native
PM source units, freezes their full dependency closure, and uses dummy SDL
video/software rendering. It activates the original Welcome default button,
opens File, moves to Edit, closes the menu, and captures the actual root frame's
retained canvas through `lx_pm_raster_read`. It terminates the process after
12 seconds; termination is deliberate, not a guest crash. It neither patches
guest code nor changes the active acceptance ball. The remembered document
in this private source ball is a Letter template, not a blank document.

The initial capture `build-lotus-visuals/actual-colors-fezgovsi/canvas-7.png`
confirmed that the custom LTCAPO10 caption was black with white text. Actual
calls during its WM_PAINT established two missing GPI contracts:

* `GpiQueryRGBColor(ps, LCOLOPT_REALIZED, CLR_BLACK)` and the same call with
  RGB `0x408080` both returned `GPI_ALTERROR` (-255). The implementation
  rejected every nonzero option and ignored the PS's logical color mode.
* The original switches to RGB mode with `GpiCreateLogColorTable(...,3,...)`
  and restores indexed mode with `GpiCreateLogColorTable(...,1,0,0,NULL)`.
  Format1 is `LCOLF_INDRGB`; it was rejected, leaving the PS in RGB mode.

The local IBM Toolkit `h/pmgpi.h` defines `LCOLOPT_REALIZED=1`,
`LCOLOPT_INDEX=2`, `LCOLF_INDRGB=1`, and `LCOLF_RGB=3`. Microsoft's original
[OS/2 Programmer's Reference Volume2](https://bitsavers.trailing-edge.com/pdf/ibm/pc/os2/OS2_1.x/Microsoft_-_OS2_Programmers_Reference_Volume_2_Version_1.1_1989.pdf),
printed pages51–52 and146–147, specifies index/RGB pairs counted in LONGs,
realized color queries, and RGB-mode query interpretation. The SDK's documented
RGB-to-indexed transition resets the logical table, including a zero-entry
INDRGB call. The host is true color, so realized and unrealized RGB results
coincide; there is no synthesized application-specific gradient.

`pmgpi.c` now resolves RGB queries through the selected PS, accepts the valid
query flags, rejects CLR_DEFAULT, and maps RGB to that PS's logical table (or
identity in RGB mode). `pmwin/graphics.c` accepts INDRGB pairs, checks the entire
array before mutation, ignores lStart for that format, and resets custom entries
when leaving RGB mode. Existing format0/CONSECRGB behavior is preserved.

The rerun `build-lotus-visuals/actual-colors-9ywqd677/canvas-7.png` visibly
restores the original caption's black-to-teal fade. Actual original queries
now return `0x000000` and the current SYSCLR active title background `0x408080`;
the zero-entry INDRGB restoration succeeds. Caption row20 has62 distinct
colors, with sampled RGB values progressing `(0,0,0)`, `(8,16,16)`,
`(38,76,76)`, `(64,128,128)`. No blue/teal override was introduced.
`pixel-comparison.json` records these measurements and matching SHA256 hashes
for the original WORDPRO.EXE and LTCAPO10.DLL in source and probe clones.

The yellow workspace did not flicker in this bounded File/Edit/menu-close
sequence: all four captures have exactly70,954 olive `(128,128,0)` pixels,
and the fixed workspace rectangle `(10,120)-(105,725)` is byte-identical.
This is a retained-canvas result with the software host, not acceptance of every
live Metal/menu/resize path. Native system titlebar/window composition was not
captured. No claim is made that every historical yellow flicker is resolved.

Regression coverage in `test_pm.c` checks per-PS custom colors, INDRGB pairs,
odd/invalid pairs without partial mutation, valid/invalid query options,
RGB-mode identity, CLR_DEFAULT rejection, and the original zero-entry reset.
The full runtime build succeeds, the focused PM/frame/menu/help-font/printer
set passes5/5, and the complete runtime suite passes66/66 in8.39seconds with
four workers (`/tmp/2ine-caption-color-full-tests.log`). `git diff --check`
passes. The original probe retains all four PNGs, raw PPMs, native digests,
launch plan and complete bounded trace under the directory above.

A final scratch-only probe adds a retained capture immediately before the
original Welcome default command:
`build-lotus-visuals/actual-colors-vjpidkm4/canvas-0.png`. The genuine frame is
511×356 with style`0x940002a0`; the original preview control (id25700) is at
(244,48), size248×252. Visual inspection shows the complete Letter thumbnail,
including its top heading and bottom paper/shadow, and all dialog controls.
There is no blank strip above the tabs/body and no preview cropping. This
uses the latest system dialog-unit conversion (6×16 at96DPI) and the original
preview: the same trace records original `GpiDrawBits` points
`{1,1;189,244;0,0;188,243}`, successful source188×243 depth8, target188×243.
There is no application coordinate patch.
The later document capture in that same probe retains the restored caption
gradient. The dialog capture is guest retained pixels at1×, not native host
titlebar/Retina presentation acceptance. Production source was unchanged by
this last capture.

## Native flicker follow-up: pattern provenance and device colors

The user's later native screenshots show a real remaining two-yellow flicker
and apparent modal text weight changes. The previous headless success does
not close that report. New bounded original probes include full invalidation,
File/Edit transitions, deactivation/reactivation, original private-document
WM_CHAR input to mark it dirty, original WM_CLOSE, and repeated full modal
invalidation. No desktop input or active-ball/document writes are used.

`build-lotus-visuals/repeat-colors-s3cm_v9j` contains the actual original Save
Changes modal at `canvas-181.png`, then `canvas-201.png`, `canvas-221.png` and
`canvas-231.png`. All four complete495×104 retained modal images are
byte-identical; the workspace comparison rectangle stays identical through
each tested document/menu/focus/full-paint transition. The text PS is repeatedly
fresh, regular font0 at16pixels, with the same indexed colors and mix defaults.
This is SDL software retained-target readback, not an independent CPU canvas
or acceptance of native Metal presentation. The font agent separately measures
actual glyph masks and partial repaints.

`build-lotus-visuals/pattern-colors-k_zcpyag` adds scratch-only pattern logging
inside the actual span writer. The original document paints into its CPU
memory bitmap in RGB mode using an8×8,1-bit checker tile, foreground`0x808000`,
background`0xc0c0c0`, both mixes OVERPAINT(2), pattern reference(99,604).
These values repeat across repaints. The resulting workspace is olive/gray,
not olive/white: its95×605 sample contains28,737olive and28,738gray pixels.
The alternating black/white source tile chooses those two primitive colors;
its white bits do not imply a white workspace background. This corrects the
earlier visual impression reported in agent status messages. Its modal and
workspace comparison JSON again records no differences on the software path.

The further scratch-only probe `build-lotus-visuals/cpu-patterns-9np2ki9v`
hashes the actual original CPU memory bitmap before GpiBitBlt copies it to
the window. Across seven original copies, the993×636,24-bit source has the
same workspace sample hash`83aa8b4ad9e38879`. The sample is x2..96,y1..605;
28,453pixels are gray and28,452olive, with the remaining570 in six source
rows outside the rendered destination. The source/destination mapping stays
source(0,0) to target(0,-7)-(993,629), ROP_SRCCOPY`0xcc`, options2. This gives
independent CPU-side stability evidence in addition to the software renderer
readback. It still does not demonstrate stable native presentation or every
possible incremental clipping region. `cpu-comparison.json` retains the seven
copy counts and raw sample hash.

The device capability audit found a separate concrete error: the display
reported CAPS_COLORS=16 while claiming1plane×24bits and16,777,216physical
colors. Microsoft's original OS/2 Reference Volume2 AppendixB, printed
pages554–556, defines CAPS_COLORS as simultaneous device colors, while
CAPS_COLOR_INDEX is the highest logical-table index and
CAPS_COLOR_TABLE_SUPPORT bit0 means RGB tables with8bits per primary.
The display now reports16,777,216colors,24bitmap bits per pel, RGB8 table
support, and maximum custom table index255. Its initial logical PS table
remains the original16entries. The existing printer-specific overrides remain.

`test_pm.c` verifies these device values, the unchanged initial palette, a
custom entry at advertised index255, and rejection beyond that limit. Focused
PM/display-DPI/bitmap/printer tests pass8/8 in2.72seconds; the full runtime
suite passes66/66 in8.46seconds
(`/tmp/2ine-device-color-caps-full-tests.log`). Build, format and diff checks
pass. This does not establish a causal fix for the native flicker: the original
Word Pro trace in this scenario queries only capabilities2,8,9,37and38, with
no query covering14/15/16/17/33/34. No application-specific pattern override,
forced color change or font workaround was introduced.

## Hidden native Metal: retained target versus pre-present window buffer

Following the axiom-graphics Metal diagnostics, the private probes explicitly
enable `MTL_DEBUG_LAYER=1` and `MTL_SHADER_VALIDATION=1`. Each successful run
logs both “Metal API Validation Enabled” and “Metal GPU Validation Enabled”,
without validation errors. These are diagnostic environment variables only.

`build-lotus-visuals/probe-native-metal.py` recompiles the current HAL video
source into a private native dependency closure. It draws the measured
olive/gray checker and a real regular16px font coverage mask into a hidden
Cocoa/Metal window, then intercepts SDL_RenderPresent to read the window
backbuffer immediately before presenting it. It separately reads the retained
texture and logs renderer/target, output/window/pixel sizes, viewport, scale,
logical presentation, clip, texture blend/filter/modulation, alpha range and
output color-space/HDR state. It does not reuse an old probe dylib or change
production HAL code.

`build-lotus-visuals/native-metal-69zunkmb` compares twelve presentations,
border/resizability changes, position changes and resizing away/back. At1×,
retained and pre-present RGB hashes match. At2×, SDL's default LINEAR sampler
changes the checker and antialiased mask, but the resulting hashes remain
stable across repeated presents and surface transitions. All retained pixels
are opaque; texture modulation is255,255,255,255, scale1, and HDR is disabled.
There is no measured frame-to-frame flicker in this fixture.

The scratch-only nearest/linear A/B run
`build-lotus-visuals/native-metal-ygqb7loo/ab-comparison.json` confirms that
nearest gives exact integer2× copies of the retained pixels; linear does not.
At1× both match the source. This identifies resampling, not a demonstrated
cause of the reported native instability. No production filter policy change
was made, and no noninteger or video-texture sampling policy is inferred.

The stronger original-binary run is
`build-lotus-visuals/native-original-zfqk6iuo`, generated by
`probe-native-original.py`. It clones only the prior private acceptance ball,
suppresses native window show/raise in its scratch HAL, and sets
`SDL_MAC_BACKGROUND_APP=1`, `SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0` and
`SDL_WINDOW_ACTIVATE_WHEN_RAISED=0`. All captured windows report HIDDEN and
none report native INPUT_FOCUS. PM focus/deactivation/menu messages and the
original dirty-document/Save Changes sequence still execute. Its22second
termination is deliberate. Earlier exploratory runs suppressed window show/
raise but did not yet set the explicit application-level background hint;
the final evidence uses all three hints.

All34 actual original Metal pre-present frames byte-match their retained
targets. Nineteen comparable workspace samples remain identical through the
tested menu/focus/full-invalidation sequence. The four complete495×104 Save
Changes images are identical. The final root canvas is1512×982, reflecting
the native display bounds; it is not the software dummy's1024×768 desktop.
The actual default native windows report1× pixel density, no logical renderer
scaling, default linear texture sampling, opaque retained pixels and HDR off.
`comparison.json` retains per-frame results and `main-prepresent.png` /
`modal-prepresent.png` are direct backbuffer captures. The accompanying
`analyze-native-original.py` can regenerate their comparisons.

This leaves the user's visible native flicker open. Hidden windows cannot
exercise real visible-window activation, WindowServer/Core Animation
composition, display migration, or live compositor resampling. Neither stable
hidden Metal output nor the nearest A/B result justifies claiming the native
report fixed. No production HAL/font/pattern change followed this negative
reproduction result.
