<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Presentation Manager applications

2ine implements a small PM/GPI subset. These GUI targets are verified on Linux
with both the native i386 worker and Unicorn:

| Target | Fixture | Verified behavior |
|---|---|---|
| PMStars | Unchanged original 1999 executable by Don Moore | Colored starfield animation, left-click origin, right-click line/dot toggle, resize and close |
| RunBox | Unchanged archived 1.01 C source rebuilt with Watcom 11.0c, static C runtime and OS/2 imports | White 50×50 window, timer-driven movement, pointer avoidance and close |

The current Hobbes PMStars 1.1 and RunBox packages contain GCC builds requiring
additional `libcn0`/`gcc1` runtime DLLs. Those builds are not the verified fixtures.
The original PMStars package identifies its compiler as Watcom 10.6. Its `void
main` returns the final `WinTerminate` result, so its observed orderly close exits
with status 1. The rebuilt RunBox returns 0.

[Hobbes mirror](https://hobbes.infania.net/) ·
[Original PMStars package](https://ftpmirror.infania.net/sites/os2bbs.com/tools/pmstars.zip) ·
[RunBox source package](https://hobbes.infania.net/download/pub/os2/games/toys/RunBox_1-01.zip).

## Run an application

Build an [application profile](build.md). A normal desktop session uses its SDL3
video backend; no OS/2 desktop or virtual machine is involved.

```sh
python3 tests/integration/fetch_gui_fixture.py --destination build/fixtures/pmstars
TWOINE_LIBPATH="$PWD/build" build/lx_loader build/fixtures/pmstars/PMStars.exe
```

The fetcher verifies both the archive and extracted executable against
`tests/integration/gui-fixtures.json`. Guest binaries are supplied separately
and are not committed to this repository. Keep an application's companion DLLs
and data files beside its executable when they are required.

## Verify pixels and input

The Linux integration harness starts its own Xvfb, uses XTest for real pointer
events, captures PNGs and checks behavior before sending a normal window-close
request. It writes a receipt with program, worker, library and screenshot hashes.
Install Xvfb, libX11 and libXtst for the host Python architecture.

```sh
python3 tests/integration/gui.py --build build --backend native \
  --program build/fixtures/pmstars/PMStars.exe --profile pmstars --expected-exit 1 \
  --work build/gui-results/pmstars
```

Use `--backend unicorn` for a portable build. `--profile runbox --expected-exit 0`
selects the RunBox movement/painting checks. A previous receipt is removed before
each run; failure cannot retain a successful receipt.

CMake optionally registers these checks when given explicit fixture paths:

```sh
cmake -S . -B build -DTWOINE_PMSTARS_EXE=/absolute/path/PMStars.exe \
  -DTWOINE_RUNBOX_EXE=/absolute/path/runbox.exe
ctest --test-dir build --output-on-failure
```

Linux application CI fetches the pinned original PMStars fixture and runs this
visual/input check. Other application profiles run the PM contract test with
SDL's dummy renderer. Neither check qualifies every physical display backend.

## Implemented boundary and limits

PM owns frame/client relationships, stable window handles, queue ordering,
coalesced timers, message coordinates and presentation-space state. GPI supplies
pixel-unit window presentation spaces, indexed colors, move/line/pel drawing and
erase. SDL3 implements host windows, textures, presentation, event translation
and finite event waits. Native guest API entries realign OS/2 stacks for host
libraries. Portable entry/callback bridges use checked fixed-width guest layouts,
including 32-byte QMSGs and copied SWPs for host-generated position messages.

Top-level windows use host decorations. OS/2 frame controls, menus, dialogs,
system icon resources, keyboard translation, bitmap APIs and font rendering are
not implemented as a complete PM environment. `WinQuerySysPointer` reports no
resource; it does not return a fabricated icon. Only pixel-unit window DCs and
direct drawing mode are supported. Existing VIO/NE execution support does not
imply 16-bit PM APIs. Qt/klibc applications require additional runtime integration.

Archive screening also considered PMxEyes, PMEyes, Eyecon, Eyes 0.9.5, PMLife,
PMGuts and several clocks. Their menu/dialog/profile, NE16 PM or external-runtime
requirements exceed this subset; they are not listed as working applications.

Lotus SmartSuite 1.7 with Fixpack 3 does not yet reach an editable document.
With its installer settings supplied separately, Word Pro creates its actual
welcome dialog on native and Unicorn. Its push buttons render through the font
HAL and respond to real pointer input. Cancel dismisses the dialog through its
normal window procedure. Create a Plain Document advances into Word Pro
Preferences, whose additional control requirements exceed the current subset.
This startup result does not qualify other suite components or document
creation, saving and reopening.

## Profiles and Open32 settings

PMSHAPI provides case-sensitive application/key profiles, binary values,
application/key enumeration, user-over-system queries and private profile
handles. Existing OS/2 binary INI files can be read. Writes use an atomic
replacement in 2ine's own profile format; they do not produce native OS/2 INI
files. A private profile's first write converts that file to the 2ine format.
The supported file limit is 32 MiB. Open handles share updates within one
worker; concurrent writers in separate workers are not synchronized.

The optional host paths `TWOINE_USER_PROFILE` and `TWOINE_SYSTEM_PROFILE` select
the built-in profiles. Their defaults are `.2ine-user.profile` and
`.2ine-system.profile` in the worker's working directory. `PrfOpenProfile` takes
a guest path through the normal drive mapping.

PMWINX implements Open32 initialization/termination, checked `MulDiv`, and
`RegOpenKeyEx`, `RegCreateKeyEx`, `RegCloseKey`, `RegQueryValueEx` and
`RegSetValueEx`. Registry keys and value names fold ASCII case, values retain
their type and exact bytes, handles enforce query/write/create access, and
undersized value queries return the required length. IBM Open32 and Win32
predefined root constants address the same roots. The separate registry profile
uses `TWOINE_REGISTRY` (a guest path), default `.2ine-registry.profile`.
Enumeration, deletion, security descriptors, Unicode name folding and other
Open32 services are not implemented.

LX resources are validated and mapped independently of fixed EXE addresses.
DOSCALLS resource-size/get/free operations retain read-only copies until their
matching releases; PM loads counted string bundles through those operations.
Window IDs, class extra words/pointers and subclassing preserve guest addresses,
including a callable default procedure returned to portable guests. Window
creation publishes its hierarchy before WM_CREATE and provides a complete
52-byte guest CREATESTRUCT. Enumeration retains a z-order snapshot and wraps
after returning an end marker. Presentation parameters inherit through owners,
notify affected windows, and support indexed-to-RGB conversion. Show/hide and
enabled state are maintained; destroyed windows retire their queued messages. PM exposes
its default system palette, basic control/screen metrics and transient screen
presentation spaces for queries; drawing into the desktop is unavailable.
GPI color queries use the same indexed palette as drawing. Custom palettes and
codepage conversion of string resources are unavailable. HELPMGR explicitly
reports unavailable help rendering through `HELPINIT`; it creates no help
instance.

The font HAL measures and rasterizes the pinned Liberation faces through
FreeType. GPI exposes public font enumeration, current font metrics and bounded
CP437 text extents; window font presentation parameters select family, style
and point size. Text extents use character advances without kerning, consistent
with the device's advertised capabilities. Glyph rasterization is covered by HAL tests. GPI text output,
logical-font selection, transformations, complex-script shaping and a complete
font/graphics API remain unavailable. Font query support alone does not qualify
an application's text editing or document rendering.

Window registries and message queues currently belong to the calling PM thread;
cross-thread window lookup, synchronous sends and queue posts are unavailable.

## Dialog and basic control subset

PM loads packed, codepage-437 dialog resources or checked in-memory templates,
creates their frame/child hierarchy and sends WM_INITDLG. Modal processing
disables an enabled owner, dispatches queued messages, retains state across
nested or reentrant destruction, restores the owner and reposts WM_QUIT.
Dismissal hides the window; WinDlgBox destroys it after its modal loop returns.
The SDK's QWS_FLAGS/FF_DLGDISMISSED and QWS_RESULT words track this state.
Clearing FF_DLGDISMISSED permits another modal run. Child and owned windows
receive destruction notifications before their parent or owner.

The stock control subset includes text push buttons, single-line text/frame
statics, and text lists with insertion, removal, queries, item handles and
selection state. Lists support single-click selection; modifier-key selection,
scrollbars, owner drawing and full keyboard navigation are unavailable.
Push buttons post normal WM_COMMAND messages to their owner. Window text is
stored as owned strings and queried with bounded, NUL-terminated copies.
Portable subclassing returns a callable guest procedure for stock controls.
Checkboxes, entry fields, combo boxes and complete frame/menu controls remain
unavailable. Unsupported controls do not receive fabricated successful handles.

Glyph masks are composited by the host HAL with clipping and coverage alpha.
This control rendering does not supply GPI text output or complete dialog text
layout. The current-thread WinPostQueueMsg and NULL-window WinPostMsg paths
support modal queue messages; cross-thread posting remains unavailable.
