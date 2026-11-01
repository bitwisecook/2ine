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

Top-level windows use host decorations. The supported subset includes menus,
accelerators, resource dialogs, file dialogs, entry/list/combo/spin controls,
checkboxes, radio buttons, multiline entries, logical fonts, text output,
memory DCs, bitmaps and clipping regions. System pointer shapes use host
cursors. System icon resources, 16-bit PM, printing, full transformations and
a complete OS/2 desktop remain unavailable. Window registries and queues are
local to one PM thread; cross-thread sends and posts are unsupported.

### Word Pro document workflow

With separately supplied SmartSuite 1.7/Fixpack 3 binaries and installation
settings, Word Pro's native Linux i386 and Linux Unicorn profiles support a basic document cycle:
create a plain document, type text, choose Courier, save an LWP, close and reopen,
choose Helv, add text, save and reopen again, then exit normally. The test uses
real X11 keyboard and pointer events and checks the resulting document's bytes.
This qualifies that bounded workflow, not complete Word Pro compatibility.
The toolbar renders when the original SmartIcons preferences and icon resources
are supplied. Frame sizing, scrollbar controls and document-menu merging still
differ from stock Warp; see the [visual review](../research/reference_screenshots/wordpro/review.md).
Reference captures live in `research/reference_screenshots/wordpro/`.
Manual macOS ARM64 testing reaches the welcome dialog and document editor.
The Edit-menu initialization failure caused by missing clipboard/atom exports
has been addressed, together with transparent dialog frames, menu tracking,
class clipping and Open32 system cursors. This does not qualify the full workflow
on macOS: initial page and toolbar drawing has intermittently required input or
a focus change, a blank menu-bar entry has been observed, and launch/foreground
behavior remains under investigation. The newest host-event pumping change has
regression coverage but still needs successful application-level confirmation.
1-2-3 is the next application; other suite applications are deferred.

```sh
python3 tests/integration/smartsuite.py --build build --backend native \
  --installation /private/LOTUSW4 --settings /private/wordpro-settings \
  --work /private/wordpro-results
```

Settings must contain `user.profile` and `registry.profile`; `system.profile` is
optional. Supply `l97user.ini` in the settings directory, or `L97USER.INI` in the
Word Pro installation directory. The harness copies application preferences
and profiles into its work directory, redirects INIPath through PMSHAPI, retains screenshots and logs,
and writes `receipt.json` only after both saves/reopens and a successful exit.
The work directory's previous harness document and receipt are replaced.
Proprietary application binaries and fonts are supplied separately.

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
`RegOpenKey`/`RegOpenKeyEx`, `RegCreateKey`/`RegCreateKeyEx`, `RegCloseKey`,
`RegEnumValue`, `RegDeleteValue`, `RegQueryValueEx` and `RegSetValueEx`. Registry keys and value names fold ASCII case, values retain
their type and exact bytes, handles enforce query/write/create access, and
undersized value queries return the required length. IBM Open32 and Win32
predefined root constants address the same roots. The separate registry profile
uses `TWOINE_REGISTRY` (a guest path), default `.2ine-registry.profile`.
Subkey enumeration/deletion, security descriptors, Unicode name folding and other
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

The font HAL measures and rasterizes twelve pinned Liberation faces through
FreeType. GPI supports logical font sets, selection, character boxes, CP437
text output and extents. Extents use character advances without kerning.
Complex-script shaping and a complete font/graphics API remain unavailable.

### Optional original Warp outline fonts

Users with Warp 4.52 installation media can extract its twelve native Type 1
faces using external [7-Zip](https://www.7-zip.org/) and
[Deark](https://entropymine.com/deark/):

```sh
python3 tools/extract_warp_fonts.py --iso /private/warp452-install.iso \
  --destination /private/warp-fonts --deark /path/to/deark
TWOINE_WARP_FONT_DIRECTORY=/private/warp-fonts build/lx_loader WORDPRO.EXE
```

The extractor requires a new destination outside this checkout and records ISO,
member and decoded-font hashes in a manifest. It decodes the compressed native
`OS2IMAGE/DISK_*/` PFB members. The fonts retain their original rights and must
remain private. They are never downloaded, installed, copied into build outputs,
or bundled by 2ine. Without the explicit environment variable, Liberation remains
the default. `TWOINE_FONT_DIRECTORY` selects an alternate Liberation directory.
This option supplies outline faces; it does not reproduce Warp's bitmap system
fonts or guarantee identical text layout.

Frame positioning supports explicit maximization within the parent, follows parent
resizing, and restores geometry through the public `QWS_*RESTORE` words. Native
minimized-window behavior remains unsupported. GPI lines and boxes share page
viewport conversion with coordinate queries; bitmap pattern fills honor their
reference point, and box outlines use line attributes separately from area fills.
Bitmap copying supports source/destination background transparency. Full model
and viewing transforms, arbitrary pattern fonts and complete text scaling remain
outside the supported subset.

## Dialog and basic control subset

PM loads packed, codepage-437 dialog resources or checked in-memory templates,
creates their frame/child hierarchy and sends WM_INITDLG. Modal processing
disables an enabled owner, dispatches queued messages, retains state across
nested or reentrant destruction, restores the owner and reposts WM_QUIT.
Dismissal hides the window; WinDlgBox destroys it after its modal loop returns.
The SDK's QWS_FLAGS/FF_DLGDISMISSED and QWS_RESULT words track this state.
Clearing FF_DLGDISMISSED permits another modal run. Parents receive WM_DESTROY while their children remain queryable; child
destruction follows before the parent storage is released.

The stock controls support bounded text editing, single selection, list search,
owner drawing, checkbox/radio state, combo selection and control notifications.
Window text uses owned strings with bounded NUL-terminated queries. Guest
subclass procedures can call the returned default procedure on both workers.
Full list keyboard navigation, modifier-key multiselection and scrollbars are
incomplete. File dialogs accept a typed single filename; allocated multiselection
lists and complete modeless behavior are unsupported.

Glyph masks use coverage alpha and visible-window clipping. Current-thread
input hooks can modify or consume queued input. Other hook types and cross-thread
hook operation remain unsupported. Unsupported API paths return failure rather
than fabricating resources.
