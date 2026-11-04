# Original OS/2 help manager under 2ine

## Target and acceptance

The user requested genuine OS/2 help from their supplied OS/2 installation media,
not an opaque handle that suppresses the current unavailable warning. This is
emulator compatibility work; no original-source reconstruction milestone changes.

Original HELPMGR.DLL SHA-256:
`14fae446692c31905461b5b960ef57c4785870476829e3e9da9818f53f0b9d37`.
Retained private fixture:
`build-original-help/OS2/DLL/HELPMGR.DLL`, copied from
`/Users/jimd/src/os2/extracted/module-packages/61e9e5894b1aa2f50c822d0ba9bb052a67734fd41f953c383549fc11764a916f/output.006.OS2_DLL_HELPMGR.DLL`.

Required companion resource DLL is **HPMGRMRI.DLL**, found in the same media's
package `bd2b204ad0b5ac7835f5526230cf0bcee4c8988f5c0229e05f5b3ccea77ef729`
as `output.054.OS2_DLL_HPMGRMRI.DLL`. Companion viewers are **VIEW.EXE** and
**VIEWDOC.EXE**, not VIEWDOC.DLL. Default help books are HMHELP.HLP,
HMHELPA.HLP, HMHELPH.HLP. Other application books remain user media.

Acceptance distinguishes: original module linkage, real instance creation with
HELPINIT.ulReturnCode zero, original book reads, contents/panel window creation,
actual text/image rendering, interaction and clean close. Earlier stages do not
prove later ones.

## Imports and implementation

`lx_dump` retained at `/tmp/2ine-original-helpmgr.txt` records the original LX32
object layout, resources, fixups and 202 unique imported ordinal/width pairs.
Modules: DOSCALLS, PMGPI, PMWIN, NLS, PMSHAPI, PMSPL, SESMGR, PMMERGE, MSG.
The exact audit is `build-original-help/import-audit.json`.

Seventeen imports were missing. New prerequisites implement private atom-table
isolation/lifetime, NLS-weighted string comparison, desktop/current pointer and
window process queries, clipped raster scrolling with damage rectangles/regions,
and bitmap scan upload. PMMERGE is now built and exports WinQueryCpType5480;
classification values and its HMQ signature come from the actual Toolkit45
`h/pmbidi.h`. Visual work supplies WinCreateFrameControls906.

Metafile loading/playback/boundary recording remain unsupported and return
PMERR_FUNCTION_NOT_SUPPORTED rather than fabricated success. Immediate display
ON and an identity default view matrix preserve existing supported rendering.
This does not qualify arbitrary image/metafile-containing help as supported.

Standalone VIEW additionally needs PMSHAPI127 WinQueryTaskSizePos. Its query now
looks up actual worker switch sessions, returns their window geometry and returns
an explicit missing-session error without changing the output for unknown
sessions. This is a real lookup, not a fabricated desktop position.

## Observed runtime progression

The ignored isolated `build-original-help/run-create-probe.py` builds a small
LX caller using original HELPMGR51/52 and PMWIN. Its C: mapping is entirely
`build-original-help`; user Documents/Lotus.2ineball is untouched.

1. Without HPMGRMRI, the original manager registers BH_CLASS_NAME, creates its
   hidden window and attempts DosLoadModule("HPMGRMRI"), then returns
   HMERR_LOAD_DLL0x2013. This reproduces the missing original companion.
2. With original HPMGRMRI installed, creation returns a nonzero genuine PM
   window and HELPINIT.ulReturnCode0. The original manager opens HMHELP.HLP,
   reads its HSP header, TOC, vocabulary/font sections, registers its window
   classes, and destroys the instance cleanly (guest exit0).
3. HM_HELP_CONTENTS0x22b creates real cover/TOC frames and paints original
   menus/controls. HM_QUERY(HMQW_TOC) returns a genuine nonzero TOC window.
4. Initial frame creation failed at resource menu101 because FRAMECDATA's real
   USHORT hmodResources truncated the resource handle65593 to57. Visual work
   now expands the live low-word module alias before menu/accelerator lookup.
5. Numbered panels are read from the book's actual resource table: header
   offsets22/24 contain count/table offset; two adjacent USHORT arrays map
   panel numbers to TOC indices. HMHELP has visible cover64027 and hidden
   menu-help4101. Source layout checked against primary Open Watcom
   [ipfcdump header](https://github.com/open-watcom/open-watcom-v2/blob/master/bld/wipfc/ipfcdump/ipfcdump.h)
   and [panel map reader](https://github.com/open-watcom/open-watcom-v2/blob/master/bld/wipfc/ipfcdump/panel.c).
6. HM_DISPLAY_HELP0x222 for actual panels creates a surviving cover/page frame
   and reads/decodes its cell into original guest data. However observed page
   WM_PAINT currently only fills background: GpiCharStringAt tracing has not
   observed body text. This is **not** qualified as full help rendering.

Current probe, stdout/stderr, guest model dump and `.json` status are retained
under `build-original-help/`. The probe's output begins with return code and
instance HWND, followed by markers/query handles; the diagnostic model variant
also writes original page state. This private fixture is not committed.

## Paint diagnosis in progress

REA/Ghidra12.2 examined the original PAGEWINDOWPROC; complete Evidence record
`ev_5121ea44855bc360a67df3e3853730091002d6be6b2c0d3f8d992d3ad382a5ff`
is retained in `build-original-help/page-window-rea.json`. Native object context
is x86 USE32. Its indirect jump-table pseudocode is incomplete and cannot be
used as an authoritative control-flow reconstruction.

Runtime-relocated object dumps establish original code object guest base
0x20074000. PAGEWINDOWPROC+0x32b9c has WM_PAINT dispatch table at+0x32c64;
entry35 points to+0x33c44, calling original paint helper+0x21cb4. The WM_SIZE
branch at+0x33874 checks original page state+0x88, and bails when zero. The
actual isolated state dump has +0x88 zero, and no formatted line array. Visual
agent is investigating whether the frame must send/apply WM_FORMATFRAME and
WM_QUERYFRAMECTLCOUNT to its application subclass for the custom help page
layout. No patch to original bytes or invented layout state was made.

A numeric CLI REA call for the helper failed because the CLI interpreted the
hex argument as a source file offset; that failed Evidence is retained and not
used as pseudocode proof. Named exported procedure queries work.

## Validation

`help_prerequisite_apis` native CTest passes on ARM64 macOS using the SDL dummy
renderer. It verifies atom-table isolation and destruction, ASCII NLS ordering,
bitmap partial scan uploads with untouched surrounding rows, actual scroll
pixels and reported damage region. Focused pmwin/pmgpi/pmmerge/loader builds pass.
Native i386 and visible original help text/image interaction remain unverified.

The optional media-backed regression is `tests/integration/help_original.py`.
Run with `--build build-image-app-runtime --helpmgr <original HELPMGR.DLL>
--resources <original HPMGRMRI.DLL> --book <original HMHELP.HLP>
--work <empty isolated directory> --backend unicorn`. It retains source hashes,
the generated LX probe, stdout/stderr and a structured `result.json`. No IBM
binaries are committed. `--require-text` adds the still-failing page-body gate.

Latest qualification: `build-original-help-qualification3/result.json`, exit0,
panel64027, genuine page HWND805437471, `panelTextDrawingObserved=false`,
`otherTextDrawingObserved=true`. The probe reports the actual page client
window, so tracing distinguishes its paint scope from the TOC title's successful
GpiCharStringAt calls. Global text tracing alone would incorrectly qualify the
blank page. Contents/window stages pass; page body remains a required gap.

Final native `help_prerequisite_apis` run passes, including an actual information
DC for bitmap upload (DevOpenDC type8 and a checked successful close). Ruff,
ty, `lxapigen.py --check` and `git diff --check` pass for these additions.

The PAGEFRAMEWINDOWPROC REA record is retained as
`build-original-help/page-frame-rea.json`, evidence
`ev_f85ec1117dc778f881261adcd40ca27b08989a9b3c63b518b58dcaf22917d589`.
It confirms an original dispatch range0x41..0x59, including frame-format
messages, but its unresolved indirect table prevents a full pseudocode switch.
The current WM_FORMATFRAME/WM_QUERYFRAMECTLCOUNT explanation remains a
hypothesis to test against runtime dispatch, rather than a proven cause.

Standalone `VIEW.EXE` was rerun after PMSHAPI127 implementation with guest
`OS2PATH=C:\OS2;C:\OS2\DLL` (the host PATH is deliberately replaced by
runtime initialization). `build-original-help/run-view-probe.py` retains the
bounded launcher probe. Latest `view-final.stderr` proves original VIEWDOC
loads, creates CLASS_BASE and its queue, and VIEW calls WinQueryTaskSizePos
without missing-import termination. VIEW exits0 after WinSendMsg4097 to the
VIEWDOC window. The child then waits in WinGetMsg; actual cross-process request
delivery/display was not qualified and is another standalone-viewer gate.
The isolated asynchronous VIEWDOC process group was explicitly stopped.

## Frame formatting and actual page rendering (2026-11-03, later qualification)

The blank body was caused by bypassing the frame subclass. IBM Open Class source
in the supplied VisualAge media establishes the protocol:
`vacpp/iocsrc/ui/baseapp/iframhdr.cpp` dispatches WM_QUERYFRAMECTLCOUNT as standard
plus extension count; `iframe.cpp` IFrameHandler::format (near 5067) and
positionExtensions (near 5221) call the default procedure to fill standard SWPs,
append extension SWPs, and return the filled count. `iframevt.inl/.cpp` identifies
mp1 as the SWP array and mp2 as the client RECTL. These are original SDK sources;
the implementation here is new emulator compatibility code.

`frame_state.c` now queries the actual subclass for capacity, maps an SWP array
and RECTL into guest-visible memory for portable execution, sends WM_FORMATFRAME,
and applies returned child positions. Default WM_FORMATFRAME fills positions and
returns count rather than recursively moving children. Default
WM_QUERYFRAMECTLCOUNT counts actual standard children. Reentrant formatting is
bounded per frame; callbacks are allowed to destroy the frame and lifetime is
rechecked. Hidden standard controls stay hidden, including native-owned system
menus. Frame/menu geometry retains the configurable-DPI helpers. The generic
SWP protocol is not special-cased for HELPMGR or its private layout state.

`build-original-help-format1/result.json` first passed the genuine page text
scope gate after this fix. The user's subsequent native screenshot established
that text visibility alone was insufficient: body text became larger and stayed
bold after the first emphasis run, and bullets were missing-glyph boxes.

### Font and glyph diagnosis

Original FATTRS tracing before correction:

- bold: id2, face Helv, fsSelection0x20, lMatch1, baseline19, average9, outline2;
- restore: id0, face Helv Bold, fsSelection0x20, lMatch2, baseline23, average11.

The original helper queries metrics and clones the forced physical face while
changing its requested emphasis. Our renderer had replaced the forced physical
match identity with the substitute bold face and treated baseline extent as em
height. This fed simulated style and enlarged size back into the original code's
normal-font clone. The host now retains the physical lMatch/design identity
separately from the selected rendering style. Explicit physical bold-face matches
still report bold-face metrics. Outline creation uses the PS character-box/default
em size, rather than reinterpreting FATTRS raster baseline dimensions as em size.
The distinction between outline character-box sizing and raster dimensions is
also documented in the original Microsoft Systems Journal March 1989 vector-font
article (archived primary publication at
https://www.pcjs.org/documents/books/mspl13/msj/msj4/).

After correction the original requests are stable:

- bold: id2, Helv, selection0x20, match1, baseline19, average9;
- restore: id0, Helv, selection0, match1, baseline19, average9.

No original bytes, styles or book data were patched. CP437 C0 graphical glyphs
are mapped to their Unicode symbols (including bullet7→U+2022 and house127),
instead of passing control-code Unicode points with no corresponding outline
font glyph. Explicit pixel font coordinates are still pixel coordinates.

REA follow-up attempts are retained as `font-change-rea.json`,
`font-helper-rea.json` and `font-call-instruction-rea.json`. The first sandboxed
attempt could not spawn the installed JDK (EPERM); outside-sandbox execution
resolved that environment failure. Numeric decompile addresses were interpreted
as file offsets; a guessed symbol did not resolve; exact instruction evidence
`ev_4b9d9d023d4ed2b03a8f4284eb8fd8b9a9ee0630fbd9a7a92dd3dbb58ef68e9c`
reported undecodable. Those attempts do not prove a helper boundary or pseudocode.
Runtime FATTRS requests and actual renderer behavior are the decisive evidence
for the font issue. A relocated object dump was also decoded with LLVM MC for
orientation; it is not presented as recovered original source.

### Pixel and regression acceptance

The corrected original page's retained renderer image is
`build-original-help/help-page-fontfix.png` (496×524), SHA256
`d10d4fee3b233db7e90df4b20f9ae8778323b9e998422d135a3c0c6cc4f8ac17`.
Visual inspection shows regular body text, isolated bold Help/F1 emphasis,
consistent line baselines and bullet dots. It was exported from the genuine
original page's retained pixels with a temporary WinEndPaint diagnostic during
an isolated SDL dummy run; that diagnostic was removed immediately and clean
pmwin rebuilt. `paint.c` is unchanged. This establishes actual page rendering,
not native window-chrome acceptance. A CUA attempt to attach the uniquely named
native review bundle timed out; the bounded probe subsequently closed itself.

Latest clean original fixture gate:
`build-original-help-clean-final1/result.json`, --require-text, exit0, genuine
page HWND and panel64027, page text observed and clean destruction. Companion
hashes: HPMGRMRI `79cadf8a0efd410c322f5950489c066befe7370c2933f9427625ad8869486782`;
HMHELP.HLP `35876e13a3db0ad739b3a7cb58d377699b24b737c7fbbb8489f4f3ccf0ffc65d`.

Persistent tests `frame_format_protocol` and `help_font_transitions` cover SWP
arrays without immediate side effects, subclass extension positions, resize,
reentrancy, forced physical match versus simulated style, three repetitions of
the original regular→bold→default clone pattern, explicit bold-face identity,
character-box size and graphical bullet glyphs. Focused native 12/12 passed in
`/tmp/2ine-help-final-tests.log`. Full suite results are recorded at
`/tmp/2ine-help-integrated-tests.log`. The optional original regression adds
--visible and --hold-seconds 0–60 for controlled manual review; these do not
change its default headless gate. Ruff, ty, source-boundary checks and diff checks
pass. No real user ball or licence was modified.

Full native suite: 60/60 passed in 44.61 seconds.

The earlier blank-page limitation is resolved for the qualified original book
panel. Arbitrary metafile images, standalone VIEW cross-process delivery, search,
all books and full help interaction remain separately unqualified; unsupported
metafile operations still return an explicit error. Passing the text panel does
not qualify those independent features.

## Existing-ball help search path, 2026-11-04

Read-only inspection of `/Users/jimd/Documents/Lotus.2ineball` after support
extraction found HELPMGR.DLL250157 bytes, HPMGRMRI.DLL21881 bytes, VIEW.EXE33304
bytes, VIEWDOC.EXE and HMHELP.HLP/HMHELPA.HLP/HMHELPH.HLP in their intended
OS2/DLL, OS2 and OS2/HELP directories. The manager, resource DLL and default
book match the hashes recorded above. CONFIG.SYS has correct
`LIBPATH=C:\OS2\DLL`, but no HELP entry; its DPATH is
`C:\OS2;C:\OS2\SYSTEM`. The manifest environment is empty. Existing ball
defaults likewise omitted HELP. Extraction therefore succeeded, while the
original manager's independent default HMHELP.HLP lookup could not find its
book even when the caller specified an absolute application book path.

Fresh evidence is confined to `build-help-current/`. The isolated
`probe-help-paths.py` copies only the three required original files and uses
the installed `/Applications/2ine.app/Contents/Resources/Runtime` runtime:

- `without-help-path/result.json`: guest exit0, original HELPINIT error0x2003
  (HMERR_OPEN_LIB_FILE, Toolkit45 pmhelp.h), HWND0.
- `with-help-path/result.json`: changing only HELP to `C:\OS2\HELP` yields
  original HELPINIT error0 and genuine HWND720306186.
- Native trace proves the failing second `DosSearchPath(7,"HELP","HMHELP.HLP")`
  falls back to `DosOpen("HMHELP.HLP")`; the successful comparison opens
  `C:\OS2\HELP\HMHELP.HLP`.

`ball/ball.c` now provides `HELP=C:\OS2\HELP` through its shared default table.
New CONFIG.SYS files contain it; existing balls receive it in the launch
environment without rewriting their CONFIG.SYS. Explicit CONFIG.SYS, manifest
and application values retain the existing override precedence.
`verify-default.py` creates an isolated ball, copies the real existing
CONFIG.SYS read-only, obtains the actual updated guest environment, and launches
the genuine installed manager: error0, HWND769523722. No active user process or
user ball was changed; the already running installer must receive the updated
environment on its next launch.

Persistent `test_help_search_default_applies_to_existing_balls_and_respects_configuration`
checks the existing-ball default, unchanged CONFIG.SYS bytes, configured search
path preservation and application override. The support suite passes19/19;
ball engine/CLI suites pass23/23; rebuild, Ruff, ty and diff checks pass. This fixes initialization's
book lookup; independent full-book/metafile/viewer interaction gates above
retain their separate scope.

## Component search paths after extraction, 2026-11-04

Original 4.52 OS2/INSTALL/INSTALL.EXE, SHA256
`7297cf99a312e658da751494ac18f69955162d17e26176ecd18b42ccce63a447`,
contains CONFIG.SYS template strings for native OS2/DLL, SYSTEM, INSTALL,
APPS, APPS/DLL, MDOS and WINOS2 search directories. Its DPATH includes BITMAP;
HELP points to OS2/HELP and BOOKSHELF to OS2/BOOK. Exact selected original
path/hash and the bounded template strings are retained in
`build-help-current/os2-config-template-evidence.json`. This is static template
evidence, not an observed complete original installation.

`ball/os2_paths.h` now derives search-list additions from selected files
actually present at their canonical installation destinations. Both the
legacy CMD/REXX support importer and category extraction use it. Native
EXE/CMD parent directories join PATH and DPATH, DLL parents join LIBPATH,
HLP parents join HELP, INF parents join BOOKSHELF, and message/bitmap data
parents join DPATH. Directory allowlists cover the native OS2, MMOS2 and
bounded network BIN/DLL/HELP/BOOK layouts, plus WinOS2 executable locations.
WinOS2 DLLs never enter the native LIBPATH. Preserved alternate media variants,
runtime cache, boot driver and unrelated source directories cannot contribute.
No DEVICE, BASEDEV, IFS, service, network configuration or unrelated setting is
copied from the installer template.

Configuration preserves all original bytes and appends only changed final
assignments. Existing custom search order stays first. Directory matching
ignores case, slash direction, surrounding quotes and trailing separators.
Existing manifest search overrides receive the same required additions while
retaining custom entries, so they cannot mask the newly installed components.
CONFIG.SYS is rechecked against its original bytes before atomic replacement.
Repeated extraction can repair paths for already present selected files without
overwriting or reimporting those files; a second unchanged pass is idempotent.
This also provides a safe replay through the existing extraction operation,
using the remembered media and selected support categories.

Fonts do not use a fabricated FONT environment variable. The current host font
renderer opens its own packaged font directory or explicit warp font override;
OS/2 PM_Fonts profile registration is a separate unimplemented extraction
integration and is not claimed by these search-list changes.

Focused extraction suite23/23 passes, including custom CONFIG search order,
new category selection, existing-file repair without payload writes, tabbed SET
assignments, case/slash/quote deduplication, both supported PATH aliases,
manifest overrides, full-extraction books/apps/network paths and exclusion of
driver/font path variables. Related ball/dependency suites38/38 pass. Fresh
logs are `build-help-current/component-path-{build,tests,related-tests}.log`.
No user ball or active installer process was changed.

### Repair after an installer replaces HELP

A subsequent read-only snapshot of the actual Lotus CONFIG.SYS showed its new
HELP assignment contains only six C:\LOTUSW4 application directories. Default
fallbacks correctly do not override an explicit configuration value; the
original manager again returns0x2003. This requires persistence of the missing
component directory rather than a forced launch-time override.

`build-help-current/verify-current-config-repair.py` copies that exact current
configuration and seven original support files into a disposable ball. It
replays the remembered nested ZIP/ISO/os2image source through the existing
`import-os2-support` action, categories `cmd,help`, modules
`HMHELP.HLP,CMDREF.INF,CMD.EXE,REXX.DLL,OSO001.MSG`, rememberSourcesfalse.
All selected payloads already exist. The extraction result has imported[];
all seven SHA256 values remain equal, and a second replay changes no bytes.
HELP retains all six Lotus directories and adds C:\OS2\HELP; BOOKSHELF becomes
C:\OS2\BOOK. The unified genuine manager probe returns error0 and
HWND2819162122. Result, before/after CONFIG, environment, extraction result and
native trace are retained under `build-help-current/current-config-repair/`.

The exact real-ball request is saved for the root agent as
`build-help-current/repair-actual-request.json`; supported invocation is
`build-image-app-runtime/2ine-ball < build-help-current/repair-actual-request.json`.
This child agent did not execute that real-ball mutation. New targeted
regression verifies a fresh ball initially contains its normal HELP path,
simulates the installer replacing it, and checks that narrow replay restores
the missing directory with no imported payloads or remembered-source changes.

The root integration run subsequently applied that verified request to the
actual Documents ball. Its result again has imported[]; the original Lotus
directories remain first and the missing HELP/BOOKSHELF directories are present.
The repair summary is persisted in the ball's logs/os2-support-path-repair.json.
The isolated genuine-manager probe using the repaired actual CONFIG returns
error0 and HWND2831089674 under the receiving runtime. The final tool suite,
including the new overwritten-HELP regression, passes199/199.
