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

The output is `build-macos/2ine Balls.app` and `build-macos/2ine-Balls.zip`.
This target fails when signing/notarization credentials are absent or Apple
rejects the submission. It never silently produces an unnotarized release.
Use an existing `notarytool` Keychain profile; do not put passwords or API keys
in the Makefile. Provision Developer ID credentials separately in Xcode/Keychain.

For local development, with the identical runtime contents and an ad-hoc signature:

```sh
make macos-app-dev
open 'build-macos/2ine Balls.app'
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
Swift. New balls use `ball.json` and `.2ineball` with no compatibility layer.

The supplied Mesa prefix is an input, not downloaded application media. OS/2
and application binaries, private fonts and installation media are never bundled.
Build commands are reproducible; notarization timestamps are inherently variable.
The build retains `swift-build.log`, toolchain metadata, icon renders and notary
submission/status/log evidence under `build-macos/`.

## Xcode and tests

Open `macos/Xcode/TwoineBalls/TwoineBalls.xcodeproj` in Xcode 27. Its source
and test folders link to the Swift package's sources; there is one implementation.
Build `make macos-app-dev` first. The development Info.plist locates its bundled
resources at the standard output path, with no Python runtime dependency.
The release bundle has no development resource path.

```sh
make macos-test
python3 tests/integration/macos_balls.py \
  --app 'build-macos/2ine Balls.app' --work build-macos/relocation-check
TWOINE_KOSMICKRISP_ICD="$PWD/build-macos/2ine Balls.app/Contents/Resources/Runtime/vulkan/icd.d/kosmickrisp.json" \
  python3 tests/integration/opengl.py \
    --build 'build-macos/2ine Balls.app/Contents/Resources/Runtime' \
    --backend unicorn --work build-macos/bundle-gl-tests
```

Run the `TwoineBalls` shared scheme's tests in Xcode for the native UI smoke test. It opens
a real ball, navigates its settings, edits/saves CONFIG.SYS, uses launch
shortcuts, verifies a missing-executable error and recovery, and creates a new
ball through the native save panel. Test captures remain in the `.xcresult` bundle. Swift package
tests verify argv, bounded/full output, exit status, preferences, and interaction
with the shared C engine. C-engine regressions verify unknown manifest fields,
installer edits, Latin-1/CRLF preservation and saved working directories.

## Native workflow and shared storage

Create a ball with Command-N, or open one with Command-O, Open Recent, a Finder
drop, or its `.2ineball` Finder association. A ball is a directory package
containing the version-1 `ball.json` and private C: drive. Existing
plain-directory balls still open. New balls use the bundled runtime unless
an external build was selected. `runtime.build` records an optional external
build path; older clients retain this additive field. CLI launches can select
the same build with `--build`. The native C helper accepts JSON as described in
[../ball/README.md](../ball/README.md).

Map installer media under Drives. Run an installer or executable through the
Ball menu, with its arguments and optional working directory. Backslashes
are literal; quotes group whitespace and `""` represents an empty argument.
Saved launchers accept an optional `cwd`; existing launchers keep their original
behavior. Removing a launcher preserves installed files. Select application rows
to copy tab-separated text; use their context menu or Command-Return to launch.
Drive unmapping and launcher removal support Undo. The manager serializes its
operations; close guest windows and wait for exit before closing or quitting it.

Use View shortcuts Command-1 through Command-5 for Applications, Drives, Settings,
CONFIG.SYS and Activity. The sidebar, native table columns, window geometry and
last section retain system/user state. Command-Comma opens application settings.
Native text controls provide standard editing/undo. Ball paths drag to Finder
or other apps and copy from their context menu. Standard semantic colors and
controls support system appearances; labels and roles are exposed to accessibility.

CONFIG.SYS drafts survive navigation. Saving compares the original disk contents
to detect installer/CLI changes, preserving original Latin-1 bytes and line endings.
Closing with unsaved drafts offers Save, Cancel and Don't Save. A conflict stays
editable until resolved. Only SET and LIBPATH affect execution, as in the CLI.
Profiles, registry, shell objects, IPC, immutable runtime snapshots and installer
receipts retain the backend's locations and formats. Binary profiles, registry
records and shell objects have no editor in this app.

This is a library/utility app with a manager window and one owned operation, not
a document editor for OS/2 applications. The manager presents GUI programs and output logs; it does not provide an interactive stdin terminal. Text copy is available for multiple
application rows; execution and launcher removal operate on one selection at a
time. Full logs are saved under `logs/macos-*.log`; Activity shows the last 64 KiB
and actual process exit status. An installer exit code does not establish that
its installed applications are functional.

## Icon and design sources

`macos/Twoine.icon` has two editable SVG layers: a wound ball of twine forms
the O, and one continuous strand leaves it to draw S/2 and a loose curl.
Native `ictool` renders Default, Dark and tinted appearances on every app
build; `macos/IconExports` retains those exports and `Twoine.icns`.
`macos/Twoine.iconset` contains the complete 16–1024 pixel macOS size set.
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
- Four Swift tests pass, including the native C-engine round trip.
- The native Xcode UI workflow passes when run individually, including creation
  through the save panel. Combined runs passed all four core tests but lost UI
  targets when another application took the foreground; the combined run is
  not qualified as passing.
- All 59 Python tool tests, Ruff and ty pass.
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
Use the separately supplied media and procedure in [balls.md](balls.md).
