# Native macOS ball manager

The manager uses SwiftUI, Swift 6.4, Swift 6 language mode and complete strict
concurrency checking. Warnings fail the Swift build. Deployment requires macOS
27 or later; the complete graphics bundle currently targets Apple Silicon.
UI state belongs to `MainActor`. Separate actors own subprocesses, preferences,
filesystem requests and process output. No shell interprets guest arguments.

## Build a complete app

The release target signs, submits to Apple's notary service, checks the result,
retrieves its log, staples and validates the ticket, assesses Gatekeeper, and
produces a distribution ZIP containing the stapled app:

```sh
make macos-app \
  SIGN_IDENTITY='Developer ID Application: Your Name (TEAMID)' \
  NOTARY_PROFILE='your-existing-keychain-profile'
```

The output is `build-macos/2ine.app` and `build-macos/2ine.zip`.
This target fails when signing/notarization credentials are absent or Apple
rejects the submission. It never silently produces an unnotarized release.
Use an existing `notarytool` Keychain profile; do not put passwords or API keys
in the Makefile. Provision Developer ID credentials separately in Xcode/Keychain.

For local development, with the identical runtime contents and an ad-hoc signature:

```sh
make macos-app-dev
open 'build-macos/2ine.app'
```

Build prerequisites are Xcode 27 with Swift 6.4, CMake, Ninja, Python 3.11+ for
build scripts only, SDL3 development files, and the Mesa EGL/Zink/KosmicKrisp
prefix described in [opengl.md](opengl.md). The runtime builds from this checkout.
`MESA_PREFIX` defaults to `build-mesa-install`. `VULKAN_LOADER` defaults to the
Apple Silicon Homebrew Vulkan loader; override it for another installation.
`CMAKE_ARGS` can supply the repository's documented offline dependency paths.

The shipped app is Python-free. SwiftUI calls the shared C engine directly and
launches `lx_loader` with structured arguments. The bundle includes the C
`2ine-ball` helper and shared library, API modules, native dependency closure,
fonts, soundfont, Mesa, the Vulkan loader and the KosmicKrisp ICD. Mach-O
references are relocated and audited; OS frameworks/system libraries remain
supplied by macOS. Nested code is signed individually before the app. Developer
ID helpers use hardened runtime; only the loader receives `allow-jit` for
Unicorn. Local ad-hoc builds omit hardened runtime because their signatures
have no Team IDs for library validation.

The common code is in [../ball/README.md](../ball/README.md), with POSIX/Win32
filesystem adapters and a language-neutral C ABI. macOS binds it statically.
The CLI and GTK adapters call the same C library; their Python interpreters
remain frontend dependencies on Linux and build/test tooling, never Mac app
runtime dependencies. No ball persistence or launch rules are duplicated in
Swift. A new ball uses `ball.json` and `.2ineball` with no compatibility layer.

The supplied Mesa prefix is an input, not downloaded application media. OS/2
and application binaries, private fonts and installation media are never bundled.
Build commands are reproducible; notarization timestamps are inherently variable.
The build retains `swift-build.log`, toolchain metadata, icon renders and notary
submission/status/log evidence under `build-macos/`.

## Version and build number

The application version is **0.3**. Each application-bundle build reserves a new
integer `CFBundleVersion`, independently of the version number. Xcode runs the
allocator on every build, including incremental builds, and processes a derived
Info.plist before signing. The packaging script uses the same allocator.

The shared counter is stored at
`~/Library/Application Support/2ine/builds/build-number`, outside checkouts,
DerivedData and build folders. A stable file lock and atomic replacement prevent
concurrent builds from reusing a number. Existing output and installed bundle
numbers also provide a lower bound. Failed builds may leave gaps; reserved
numbers are never reused. Invalid counter contents fail the build rather than
silently restarting the sequence.

Build agents must retain this state across runs. Set `TWOINE_BUILD_COUNTER_DIR`
(or pass that Xcode build setting) to a persistent shared directory when builds
run from multiple machines. Independent machines without shared state have
independent counters. Preserve the counter when migrating build infrastructure.

## Xcode and tests

Open `apps/macos/Xcode/TwoineBall/TwoineBall.xcodeproj` in Xcode 27. Its source
and test folders link to the Swift package's sources; there is one implementation.
Build `make macos-app-dev` first. The development Info.plist locates its bundled
resources at the standard output path, with no Python runtime dependency.
The release bundle has no development resource path.

Release bundles include `TwoineBall.icns`, rendered from the existing twine
vector separately from the application icon. The `.2ineball` document and
exported package UTI both reference it; an individual ball carries no Finder icon
resource files. Invalid saved host-runtime paths fall back read-only to this
app's bundled runtime, and saved native preferences use its declared Unicorn
backend without changing the ball's settings.

```sh
make macos-test
python3 tests/integration/apps/macos/macos_ball.py \
  --app 'build-macos/2ine.app' --work build-macos/relocation-check
TWOINE_KOSMICKRISP_ICD="$PWD/build-macos/2ine.app/Contents/Resources/Runtime/vulkan/icd.d/kosmickrisp.json" \
  python3 tests/integration/os2/opengl/opengl.py \
    --build 'build-macos/2ine.app/Contents/Resources/Runtime' \
    --backend unicorn --work build-macos/bundle-gl-tests
```

Run the `TwoineBall` shared scheme's tests in Xcode for the native UI smoke test. It opens
a real ball, navigates its settings, edits/saves CONFIG.SYS, uses launch
shortcuts, verifies a missing-executable error and recovery, and creates a new
ball through the native save panel. Test captures remain in the `.xcresult` bundle. Swift package
tests verify argv, bounded/full output, exit status, stop/force-stop recovery,
preferences, runtime reset, application directory summaries, and interaction with
the shared C engine. Model tests cover failed and successful mapping, Undo/Redo,
settings drafts and default-folder resolution. C-engine regressions verify unknown manifest fields,
installer edits, Latin-1/CRLF preservation and saved working directories.

## Native workflow and shared storage

Create a ball with Command-N, or open one with Command-O, Open Recent, a Finder
drop, or its `.2ineball` Finder association. A ball is a directory package
containing the version-1 `ball.json` and private C: drive. An existing
plain-directory ball still opens. A new ball uses the bundled runtime unless
an external build was selected. `runtime.build` records an optional external
build path; older clients retain this additive field. CLI launches can select
the same build with `--build`. The native C helper accepts JSON as described in
[../ball/README.md](../ball/README.md).

In Drives → Map Drive, choose Folder or ISO or Floppy Image, select a source,
and assign a letter (for example D: for an installation CD or A: for a floppy).
Images use internal ISO 9660/Joliet, FAT12/16/32, exFAT or standard HPFS v2
filesystems, with no host mounting or extracted drive folder. CDs are read-only;
floppies and disk images default to read/write with selectable read-only
protection. The browser shows the source filename and supports the same program
and working-directory pickers. Unmap and Undo/Redo retain device metadata.
Use original installation media; mounting it does not boot OS/2 or establish
that every installer or installed application is supported.

A new macOS ball reserves A: as an ejected floppy and D: as an ejected CD drive.
The title-bar CD, floppy and disk menus show each drive letter and source
filename, attach/replace media, eject without removing the letter, and add devices.
The Ball menu provides the same controls. Create Floppy Image offers 360 KB,
720 KB, 1.2 MB, 1.44 MB and 2.88 MB. Create Disk Image offers FAT32, exFAT and HPFS.
Ball Settings can lock a device to its image and access mode, preventing eject,
replacement and mode changes until unlocked.
Media can change while a guest is running; existing file handles keep their
previous member snapshots until closed.

Choose Ball → Install Software. Attach a CD or floppy image and review the
suggested installer path. Suggestions include common EXE and CMD names in the
root and installation/language folders; Browse Drives can select any file.
CMD and BAT selection uses the original OS/2 CMD.EXE from mapped OS2 folders,
the OS/2 installation CD, or a guest COMSPEC setting. Installers start in their own folder unless an
explicit directory override is selected. After actual exit, the manager offers
to keep the image attached or eject it, while retaining the device letter.
Locked devices cannot be ejected. Built-in formatting creates new images; the
original OS/2 FORMAT utility still needs raw-device API support. See the
[filesystem limits](../ball/README.md#image-backed-drives) before using existing
HPFS media.

Run an installer or program through the Ball menu. Its native chooser browses mapped drive letters, with a drive
selector, OS/2 path field, folder navigation and file table. Backslashes
are literal; quotes group whitespace and `""` represents an empty argument.
Each drive has a saved default working directory. Use Drives → Set Default
Folder to change it. Drives is the first section, with a minimum-width drive
letter column. Drop files/folders onto a drive row to copy to its root, or onto
its browser/folders to copy there. Import Files in the browser or Ball menu
(Shift-Command-I) provides the same copy operation through a native file chooser,
targeting the displayed folder. Conflicts are reported without replacing
existing files; symbolic links are rejected.

Add/Edit Application has General, Working Directories and Environment tabs.
Overrides inherit independently for each drive/variable. For example,
`PATH=%PATH%;C:\DOOM` appends to the ball's guest PATH without changing its
stored default or the host PATH. The shared core expands the expressions and
prepares all drive directories; guest subprocesses inherit the live directories.
The default launch cwd is the program drive's configured default, initially its
root. Existing explicit `cwd`/`--cwd` launchers still override it.

Applications imports program objects registered by installers with the OS/2
Workplace Shell, including their names, arguments, working directories and icons.
Refreshing a ball reconciles those registrations while preserving manual launchers.
Editing or removing an imported launcher prevents later refreshes from replacing
or recreating it. Logs provides Open Application Registrations for the persisted
registration report.

Extracting OS/2 support also adds the standard search directories for the selected
installed components to CONFIG.SYS. Existing custom path order is preserved;
repeating extraction can repair missing paths without replacing installed files.

Applications display their registered or embedded OS/2 icon by default. Choose Icon imports a
custom image into the ball; Use Executable Icon restores automatic extraction.
Missing/unsupported embedded artwork uses the native application symbol. Removing a launcher preserves installed files. Select application rows
to copy tab-separated text; use their context menu or Command-Return to launch.
Drive mapping/replacement, unmapping, settings changes, runtime selection and
launcher removal support Undo/Redo. Failed edits do not add history. Undo returns
to the affected ball, and operation-specific menu labels identify the change.
Mapping failures keep the sheet and its entries available for correction. Missing
paths, access denials, non-directory paths and allocation failures have distinct
shared-core diagnostics; the manager displays the underlying error.
Settings drafts remain with their ball across navigation and unrelated changes.
Use Bundled Runtime removes an external runtime selection; the backend has a
separate inheritance control.

The manager serializes operations. Close guest windows normally, or choose Stop
Operation from Logs or the Ball menu (Command-Period). Stopping requires
confirmation because guest work may be lost. Force Stop is available after Stop
if the guest ignores termination. Both paths wait for actual process exit and
retain its log and exit status before allowing another operation or quitting.

Use View shortcuts Command-1 through Command-4 for Drives, Applications, Settings
and Logs. The Ball menu also exposes selection-aware application
launch/edit/remove and drive unmap/default-folder commands. File → Save CONFIG.SYS
uses Command-S. Finder commands reveal packages/files without opening them;
Open Full Log opens the log in its associated application. The sidebar, native table columns, window geometry and
last section retain system/user state. Command-Comma opens application settings.
Native text controls provide standard editing/undo. Ball paths drag to Finder
or other apps and copy from their context menu. Standard semantic colors and
controls support system appearances; labels and roles are exposed to accessibility.

CONFIG.SYS drafts survive navigation. Saving compares the original disk contents
to detect installer/CLI changes, preserving original Latin-1 bytes and line endings.
Closing with unsaved drafts offers Save, Cancel and Don't Save. A conflict stays
editable until resolved. Only SET and LIBPATH affect execution, as in the CLI.
Profiles, registry, shell objects, IPC, immutable runtime snapshots and installer
receipts retain the backend's locations and formats. Settings provides binary-safe file and registry value editors. Workplace
Shell objects remain application-managed.

This is a library/utility app with a manager window and one owned operation, not
a document editor for OS/2 applications. The manager presents GUI programs and output logs; it does not provide an interactive stdin terminal. Text copy is available for multiple
application rows; execution and launcher removal operate on one selection at a
time. Full logs are saved under `logs/macos-*.log`; Logs shows the last 64 KiB
and actual process exit status. An installer exit code does not establish that
its installed applications are functional.

## Icon and design sources

`apps/macos/Resources/Twoine.icon` has two editable SVG layers: a wound ball of twine forms
the O, and one continuous strand leaves it to draw S/2 and a loose curl.
Native `ictool` renders Default, Dark and tinted appearances on every app
build; `apps/macos/Resources/IconExports` retains those exports and `Twoine.icns`.
`apps/macos/Resources/Twoine.iconset` contains the complete 16–1024 pixel macOS size set.
The rendered result becomes the app's multi-resolution `.icns`.

The implementation applies [Axiom](https://github.com/CharlesWiltgen/Axiom),
[Icon Composer skill](https://github.com/giginet/apple-icon-composer-skill), and
[Mac-Arsed Mac App skill](https://github.com/bartreardon/skills/blob/main/mac-arsed-mac-app/SKILL.md),
with [Apple's macOS HIG](https://developer.apple.com/design/human-interface-guidelines/designing-for-macos/),
[windows](https://developer.apple.com/design/human-interface-guidelines/windows),
and [toolbars](https://developer.apple.com/design/human-interface-guidelines/toolbars).

## Validation record

Validated on Apple Silicon macOS 27.2, Xcode 27.2 (27B52), Swift 6.4.2:

- Strict Swift package and Xcode builds pass with warnings as errors.
- Five Swift tests pass, including the native C-engine round trip.
- The native Xcode UI workflow passes when run individually, including creation
  through the save panel. Combined runs passed all four core tests but lost UI
  targets when another application took the foreground; the combined run is
  not qualified as passing.
- All 65 Python tool tests, Ruff and ty pass.
- A generated OS/2 executable passes bundled-runtime installer and saved-launch
  checks, including cwd, argv, receipt identity and recovery from a missing file.
- All eight LX OpenGL scenes pass with the bundled Mesa/Vulkan/KosmicKrisp stack.
- Native icon rendering and the non-system dependency audit pass.

Developer ID Application credentials were not present during implementation,
so Apple notarization and a clean downloaded Gatekeeper installation remain
unverified. The release target implements and requires those checks. Full
VoiceOver, alternate-display and increased-contrast manual checks remain open;
the UI test exercises native accessibility elements and keyboard commands.
The macOS automated description audit also reported unlabeled framework layout
groups around `NavigationSplitView`; the audit is not qualified as passing.
The standalone C CI matrix includes Linux, macOS and Windows, but the new
Linux/Windows jobs have not been run from this checkout.

An installed SmartSuite tree was available, but original installer media,
CMD.EXE, SYSLEVEL.OS2 and OSO001.MSG were not found. The original all-components
macOS installation and Word Pro font/save/reopen qualification remain unverified.
Use the separately supplied media and procedure in [ball.md](ball.md).

## ZIP archives

The toolbar and File menu expose **Import Ball from ZIP** and **Export Ball as
ZIP**. Export's native save panel has an **Include disk images** checkbox,
initially off. Import chooses a ZIP and a new `.2ineball` location, then selects
the imported ball. Dropping a ZIP onto the manager opens the same import flow.
An existing ball is never replaced. Export saves a pending CONFIG.SYS draft
using its normal conflict check before creating the ZIP. Archive work runs on
the backend actor, with guest launches and media changes blocked until complete.

Every export includes `2ine-export.json` with engine Git version/revision and
build details, app version/build, selected runtime settings and runtime content
fingerprints when available. New runtime builds also supply their own version,
compiler, architecture and build configuration. Images included in the archive
use paths relative to the imported ball. Without images, media devices import
as ejected. Folder paths inside the ball are normalized to relative paths.
Unavailable external folders also appear as ejected drives with their previous
paths shown. Select the drive and choose Change Mountpoint or Choose Folder
to reconnect it. The drive letter, launchers and working directories remain
unchanged. Runtime selection resets to the receiving host’s bundled runtime;
cached native binaries are recreated locally. Original runtime build/version
details remain in the export record.

## Sound banks and source credits

Settings → Graphics & Sound provides [sound-bank installation and 16-channel MIDI mapping](sound-banks.md), DPI, title bars, OpenGL, DIVE, video and audio controls.
About prominently credits original author Ryan C. Gordon and lists the
[project versions, authors, sources and downloads](project-sources.md).

Settings (Cmd-3) groups General, Graphics & Sound, Environment, Printing,
Config Files and Registry. Config Files lists CONFIG.SYS, OS2.INI, OS2SYS.INI
and discovered INI files by filename. Environment shows effective values and
their source, including CONFIG.SYS and ball overrides. While a guest runs, CONFIG.SYS follows
installer changes automatically. An unsaved draft is retained and a conflict is
shown if the file changes. Saving preserves Latin-1/CRLF and checks its original
contents.

INI profile editors work on application/key records. Registry uses a key tree
and typed values, including strings, multi-strings, DWORD and QWORD; unfamiliar
or malformed records retain exact hexadecimal bytes. Stop the guest before editing these
cached databases. Saving checks the file revision and preserves unedited values.
Native OS/2 INI files are read and saved in the same 2ine profile format used by
the runtime. Registry values retain their numeric types.

Display DPI is stored per ball and applies at next launch. Choose 96, 120, 144 or
192 DPI under Settings → Graphics & Sound. `system.display_dpi` also accepts whole
numbers 72–384 through Settings. Fonts, dialog units, physical drawing units and
control metrics share the setting; printer DPI stays at 300. Applications with
custom pixel layouts must still honor the OS/2 DPI APIs.

Colour depth is also per ball and applies at next launch. `system.display_depth`
accepts 4, 8, 16 or 24 and defaults to 24. It sets the depth OS/2 applications
are told about, so they choose palettes and dithering as on that display; 2ine
still draws in true colour.
