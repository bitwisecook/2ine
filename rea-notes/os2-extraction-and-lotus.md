# OS/2 extraction and Lotus follow-up

User-authorized tooling/runtime work, 2026-11-03. This work does not advance the
original OS/2 reconstruction milestone. Original media and Lotus files remain
private fixtures; no proprietary payload is included in source. Probes use
isolated ball copies, preserving `/Users/jimd/Documents/Lotus.2ineball`.

## Acceptance and implementation

The macOS workflow starts with a native media chooser, then **Browse OS/2 Media**,
then extraction review. ZIP-contained ISOs are detected automatically. CMD,
installer support and classic original REXX default on; fonts, sounds/images/video,
printer files, full help and full nonprovided OS file set default off. Existing
files and configuration are preserved. Shared `ball/os2_extract.h` provides the
plan/import actions; category-less CLI import retains its support-only behavior.
GNOME uses the same actions and three stages in `tools/gnome_media.py`.

Nested containers use seekable `TbReadStream` parents. Stored ZIP/ISO members
use direct slices; compressed members use a bounded lazy decode cache, replaying
when a backwards read falls outside it. Intermediate ISO images are not written
to disk. Only selected leaf files are materialized for copying. This is bounded
streaming, not a promise of constant-time random access into a deflated member.
`readStats` exposes decoded bytes, peak cache, slices and leaf materializations.
`ball/media.c`, `ball/media_stream.h`, `ball/disk_image.c` own these paths.

Source priorities distinguish installed paths, packed base payloads, reduced boot
files and known language/display variants. Full extraction preserves ambiguous
variants with provenance instead of silently selecting a DLL version; selective
module recovery stays strict. Original ZIP implode support and actual full-media
acceptance are being completed by the extraction agent; consult its final notes.

Both custom native browsers now have fuzzy filename search, keyboard search
capture, Name/Size/Date Modified sorting and alternating row backgrounds.
Contextual **Relevant Files Only** starts on: executable/command files in program
pickers, folders in folder pickers, and OS/2 media/installable files in the media
browser. Folders remain navigable. Turning it off exposes other files. The native
macOS search uses NSSearchField; typing from a focused table transfers input into
that field. First-focus insertion explicitly places the caret after the initial
character. No system-wide keyboard hooks are installed.

Host paths under the current home directory display with `~`; saved paths,
request arguments, drag/drop URLs and navigation retain the full values. Boundary
matching does not abbreviate a different user whose name shares a prefix.
Guest C: paths are unchanged. Source timestamps are used where present in host
files, ZIP/libarchive, ISO9660 and FAT/PACK metadata; unavailable timestamps show a
dash. No timestamp is invented for an implicit archive directory.

## Runtime and configuration work

- [Dependency audit](os2-dependencies.md): static audit after every installer,
  persistent human/JSON reports, native-provider comparison and original-library
  recovery from remembered installation media. Failed/cancelled installers get
  read-only audit output. Optional Notes/database imports remain distinguishable
  from the dependency closure of an application that was actually launched.
- [1-2-3](lotus-123-startup.md): genuine original SOM closure and loader diagnostics,
  checked file-lock API, original startup fixture reaches its named frame/message
  loop. Full spreadsheet/OLE operations and clean interactive exit remain separate
  qualification gates.
- [Printing](printing-runtime.md): PDF and PCL raster printer queues, native host
  save/print dialogs, checked job/spooler ABI. Physical-printer output, actual
  commercial application jobs and Windows runtime remain unqualified.
- [Video](video-runtime.md): pinned LGPL FFmpeg source profile, MCI digital-video
  playback, retained frame upload and synchronized shared-mixer audio. Lower-level
  MMIO/codec APIs and commercial movie-player UI remain outside the passed gate.
- [Configuration](configuration-ui.md): live CONFIG.SYS refresh with preserved
  drafts/conflict checks, binary-preserving user/system profile and registry
  editors, configurable display DPI, portable C actions and native platform views.
- [Help](original-help.md): genuine HELPMGR/MRI/viewer extraction and imports.
  Frame subclass SWP formatting now permits actual panel text. User's screenshot
  exposes bad font/style transitions, tight line spacing and missing bullet
  glyphs; text visibility alone is not full rendering acceptance. Investigation
  continues in the help agent.
- [Lotus visuals](lotus-visuals.md): frame borders, combo arrow clipping, inherited
  colors and expected default messages repaired. Real Word Pro menu flashing and
  dictionary startup error still need end-to-end qualification. Dictionary files
  are present; copying another spelling DLL is not an established fix.

The visible "Ball section" label has been removed while retaining its
accessibility label. Application rows show actual executable icons, and new
launcher names use checked original executable descriptions with editable
fallbacks. Portable package icons and stale host-runtime overrides are being
finished by the video/portability agent; consult its handoff before claiming that
raw-ball relocation is qualified.

## Root verification record

- Original OS/2 4.52 ISO: default CMD/REXX imported 11 files; opt-in
  fonts/help/printers imported 782. No fonts/help/assets were added by the default
  import. Full extraction exposed real variant conflicts and legacy ZIP codecs;
  the extraction agent owns the remaining actual-media gate.
- Stored and deflated 64 MiB ISO-in-ZIP fixtures demonstrate no intermediate disk
  materialization; nested mixed-format and deep nesting regression fixtures pass.
- Shared engine browser/support suites: 30 tests passed before the latest new
  metadata case; metadata/support suite then passed 17 tests. Retained logs:
  `/tmp/2ine-browser-core.log`, `/tmp/2ine-browser-metadata-tests.log`.
- Swift strict-concurrency/warnings-as-errors build and tests pass: 16 model and
  12 core tests in `/tmp/2ine-browser-swift-final.log`. Search matching, relevance,
  home-directory boundaries, default extraction and live configuration covered.
- Native CUA review: actual application icons visible; chooser → nested ISO
  browser → six default options → extract/completion worked in an isolated ball.
  Latest review visibly confirms striped rows, sizes/dates and column headers.
  First-focus typing issue was found and corrected; final UI recheck pending.
- A restricted-process review launch aborted in macOS HIServices registration
  before App.main; normal Launch Services launch with explicit private-ball
  environment succeeded. Do not classify that test-launch failure as a guest
  CPU or browser rendering crash. Use normal Launch Services for native UI probes.
- A full runtime test pass during concurrent `Window` layout edits had four stale
  dependent binaries. The help agent is rebuilding all header dependents and
  owns the coherent final PM/Open32 regression results.
- GNOME files compile and pass Ruff; no local GTK/PyGObject runtime or running
  Linux Docker/Lima instance is available. Its actual UI gate remains unverified.

Local app review profile uses OpenGL off, extended MIDI off and optimized
embedded Unicorn. Current runtime build uses the explicit system FFmpeg provider;
the video agent separately qualified the pinned source provider. Do not label
that review bundle a full-feature release profile. All changes remain shared,
uncommitted work until final integration and authorized push to `fork/main`.

### Subsequent accepted evidence and current blockers

Full4.52 extraction completed:14,571 imported files,457 preserved conflicting
variants,527.5s. Every variant hash was checked; native DOSCALLS/PMWIN originals
were excluded. See [legacy media](os2-legacy-media.md). Common ARJ/ARC and ZOO
follow-up is in the media agent; obsolete unsupported modes must remain explicit.

Native browser visual acceptance is complete: Finder-style striped rows,
relevance checkbox defaulton, off reveals nonexecutables, `wp` typed from the
file table reaches the native field intact and finds only WORDPRO, clear restores
all files, ~ paths displayed. Search now embeds a background-free native search
cell in regular interactive NSGlassEffectView. Cell layout/editability and native
focus ring are preserved. Real system appearance remains automatic. Retained
native screenshots were inspected through CUA; no global appearance setting was
changed.

Original help retained page rendering now passes visual review, with regular body,
isolated bold emphasis, consistent baselines and bullets. Entire native suite
then passed60/60. See original-help.md for imagehash and limitations.

Current Word Pro blockers are application-level: Welcome template treated client
size as outer frame size, titlebar zero extra bytes rejected application userdata
and caused an endless paint loop/black app surface, and previous native procedure
needs a guest-callable bridge. The printing/frame agent owns these corrections.
The actual Create a Plain Document fault has been reconstructed to null queue
DRIVDATA; root implemented and guest-tested the printer fix above. A new coherent
probe is being prepared by the video/portability agent. Original dictionary
failure still needs reproduction after the document startup blockers are fixed.

### Integrated desktop and runtime checks (2026-11-03, later)

The unified runtime passes64/64 CTest checks (`/tmp/2ine-integrated-runtime-tests.log`).
The shared extraction/audit/configuration/feature/portability engine passes72 unit
checks (`/tmp/2ine-final-ball-unit-tests.log`). Swift strict-concurrency builds and
model/core tests pass after the concurrent singular package naming cleanup.
Legacy saved ball-list keys and preference filenames remain readable; migration
writes the current file and leaves the legacy source unchanged.

Graphics & Media configuration is now exposed in both desktops: DPI, OpenGL
runtime-default/Mesa/original/off, DIVE, video, PCM and MIDI. The native dark-mode
controls, PCM off/reset, and unavailable-original reason were checked through CUA
in a private ball. Receiving-build metadata is included in extraction/audit and
post-install requests. GTK source/syntax checks pass; Linux interaction remains
unqualified. Original OpenGL availability is not a compatibility claim.

System-menu controls retain their API text while painting a square glyph rather
than clipped `System` text. Merged MDI SC_SYSMENU entries use the same glyph.
Top-level WC_MENU popups are borderless, including dynamically created system
menus. Native `pm_menu_chrome` checks exercise child title geometry, activation,
dismissal and merged action-bar placement. Actual Word Pro menu visual acceptance
is currently blocked by the following newly observed native display issue.

A fresh immutable47-image diagnostic closure loads each native module exactly
once. Its initial window has content and the earlier black-canvas startup issue
is corrected. The actual Retina Welcome window nevertheless displays oversized
content clipped inside the original outer size, despite TWOINE_DISPLAY_DPI=96.
Do not claim the current native dialog geometry is visually accepted or label
this a saved144-DPI preference. Hidden dummy/native renderer tests alone do not
establish visible Retina correctness; bounded font/dialog/HAL tracing is active.
The older live probe and its user-typed unsaved document remain untouched.

### Final integrated checkpoint

The initial display blocker above is resolved: software-only native-surface
activation fixes software canvas loss without retaining a stale Metal drawable.
The first native Welcome screenshot fits without focus cycling. Nested modal
presentation now defers only a shared canvas under paint; the real spelling
warning consequently becomes visible instead of black. Its language-zero cause
is independently reproduced and fixed through read-only NLS-backed PM_National
profile defaults; the real original initializer returns16 for0409 with no warning.

Original PMMERGE and DDK font evidence establishes system-font dialog units with
signed nearest rounding. WinMapDlgPoints now uses that contract, retaining the
separate Helv text metrics and providing a248x252 preview pane for the original
188x243 thumbnail. OS/2 fixed-size dialogs now reject host-user resizing; guest
programmatic sizing remains available. See preview-font-metrics.md and
wordpro-spelling.md for hash-bound evidence and remaining native visual gates.

One Settings page now contains General, Graphics & Sound, Environment, Printing,
Config Files and Registry. The native key tree, malformed-record warnings and
exact unknown-type hex editor were inspected in a private fixture. The graphics
page shows Mesa as the default, title chrome policy, video/PCM/MIDI/DIVE, speaker
volume and sound banks. Default PDF and source labels are visible. Core/profile
and real-LX tests verify printer overrides and stale-write behavior. Config Files
lists standard virtual profiles and bounded existing INI discovery; generic
writes are revision checked and confined to private C. GTK source checks pass,
but Linux visual execution remains unqualified.

Final runtime CTest66/66, tooling unittest165/165, Swift model16/core20 pass.
Bindings, architecture, Ruff, ty, catalogue and pinned C formatting checks pass.
No proprietary binary/media payload is tracked. Diagnostic apps/balls and logs
remain ignored local evidence. Native acceleration is a proposed phased design
in multimedia-acceleration-plan.md; it does not claim the missing APIs are done.

## Media performance and direct Help follow-up (2026-11-03 evening)

This supersedes the three-stage macOS workflow and replay-only DEFLATE description
above. Following the user's correction, Applications offers one Install Components
action and the sheet has one Install button; Help is a component checkbox. The
source is remembered per ball before copying; component selection, installed
checkboxes and missing-file counts share one sheet. Browsing is optional. Explicit
remembered folders inside an ISO stay at that folder instead of searching their
contents for more disc images. The final native UI check verified the saved
`os2image` source and ticked Installed CMD/REXX and Help. The earlier separate Help
action and Up to Date button label were removed at the user's request. A regression
fixture covers an unrelated invalid ISO inside a saved
folder; the previous reopening search failed on that fixture and the real disc.

The media cache retains at most four exclusively leased source states, each with
sixteen parent readers and a 64 MiB decoder/index budget. Source identity includes
file identity, size, modification and change times with fractional precision.
Concurrent requests never share a mutable stream cursor. ISO/FAT directory records
retain file/cluster offsets and are populated lazily. Standard ZIP DEFLATE readers
retain zlib inflateCopy checkpoints every 4 MiB; stored members retain direct
slices. ZIP64 and other codecs use the bounded library fallback. Intermediate
images remain in streams rather than temporary files. See the upstream zlib
manual's inflateCopy contract: https://zlib.net/manual.html .

Sampling the supplied 514 MiB Warp Server ISO exposed repeated macOS zoneinfo
reloads in mktime. Media requests now snapshot the local zone and cache validated
DOS timestamp conversions, releasing calendar state at request end. Mounted
writable volumes retain their ordinary live local-time behavior. The first
calendar probe crashed because an extra argument supplied a null component format
to CFCalendarComposeAbsoluteTime; this was corrected before packaging. Timestamp
regressions compare winter/summer values in UTC, New York and London and refresh
between requests. The full-root 17,655-file plan fell from 84.2 seconds to roughly
11–14 seconds locally. These are backend measurements, not another laptop's UI
latency. The original direct ISO folder listing was already about 8 ms; the new
cache also avoids repeating image-directory parsing and compressed-image replay.

The real ISO's `os2image/FI/FONT/moekai.zip` contains damaged compressed data.
Independent tar extraction matches the native member bytes; independent zlib
expands 11,290,846 bytes against a declared 11,290,996, with CRC 8f5ad610 instead
of 5fd3038b. The reader preserves integrity checks. The requested optional-font
policy skips this member and reports its source/path and size/CRC failure;
required files and explicit module recovery remain strict. A mixed fixture proves
that readable Help and fonts still install. The actual damaged font was also
skipped successfully in the isolated ball.

A fresh isolated Help installation copied 150 files in 7.7 seconds, including
HELPMGR.DLL (250157), HPMGRMRI.DLL (21881), VIEW.EXE (33304), and VIEWDOC.EXE (31511).
At the user's request, the same Help set was then installed into the real Lotus
ball in 7.4 seconds, preserving existing files and remembering the source. The
original VIEW launcher opened CMDREF.INF and spawned VIEWDOC without a missing
import abort in the isolated ball; this is startup evidence, not full help-panel
visual acceptance. The probe child was stopped after inspection.

The reported installer abort is separately identified by the real log's missing
PMWINX ordinal 544, REGDELETEKEY. A leaf-key implementation plus explicit portable
guest bridge now handles deletion, missing keys, access checks and nonempty keys.
The real-LX desktop registry test passes. A presentation regression also covers
retained damage on a child reduced to zero size by WM_ADJUSTWINDOWPOS; such a child
cannot receive WM_PAINT and must not block its entire host canvas until Cmd-Tab.
The actual SmartMaster reinstall stage has not been rerun for visual acceptance.

Validation: runtime CTest 70/70; macOS model/core 23/22; original OS/2 media tests
48/48; new cache/seek/invalidation/concurrency/font/source-reopening checks 6/6.
Generated bindings, source boundaries, Ruff, pinned C formatting and app signature
checks pass. Local evidence remains under ignored `build-media-browser-perf`.
