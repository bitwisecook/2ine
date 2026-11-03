<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Portable ball document icons and runtime selection

Parent requested a standalone twine-ball Finder icon, GNOME support where
possible, and moved-ball qualification. All changes are in shared tools/2ine;
no user ball, installed application or default file association was modified.

## Icon implementation and evidence

`tools/icons/TwoineBall.svg` reuses the existing artwork from
`macos/Twoine.icon/Assets/ball.svg`, changing only its viewport so the ball fills
a centered transparent square. `tools/render_svg.swift` uses native AppKit
SVG rendering and a transparent RGBA bitmap. `tools/build_macos_app.py`
produces all standard iconset sizes and `TwoineBall.icns`, and declares it in
both CFBundleDocumentTypes and UTExportedTypeDeclarations. LSTypeIsPackage
is explicit. The Xcode development plist matches, and its resource phase copies
the same generated icon from the documented development bundle. The parent
handles final application deployment/registration;
the installed Finder lookup has not been requalified here.

Primary Apple references:

- https://developer.apple.com/documentation/bundleresources/information-property-list/cfbundledocumenttypes/cfbundletypeiconfile
- https://developer.apple.com/documentation/bundleresources/information-property-list/utexportedtypedeclarations/uttypeiconfile
- https://developer.apple.com/documentation/bundleresources/information-property-list/cfbundledocumenttypes/lstypeispackage
- https://developer.apple.com/documentation/uniformtypeidentifiers/uttypepackage

Actual native artifacts: `build-document-icons/icon-renders/TwoineBall.png`
(1024px transparent image, visually inspected) and
`build-document-icons/Resources/TwoineBall.icns` (1,386,869bytes). ICNS chunks
include ic04/ic05/ic07/ic08/ic09/ic10 and retina ic11/ic12/ic13/ic14 plus info;
the container length and all chunk boundaries were inspected. Normal sandbox
iconutil incorrectly reported Invalid Iconset; the identical native encoder
command succeeded with ordinary host access. Rendering itself and every PNG
size succeeded inside the workspace. The build script's normal unsandboxed
developer invocation requires no extra image library or external SVG converter.

`tools/gnome_ball_icons.py` uses Gio metadata::custom-icon-name with the themed
name application-x-2ine-ball. It does not set an absolute custom-icon URI or
write a file/xattr into the ball. Existing user custom icons are preserved.
Metadata absence/failure is optional and does not prevent opening the ball.
The frontend applies it in worker-side snapshot/open/create/import paths.
The GNOME installer and package stage install the vector in scalable/mimetypes;
the MIME declaration references that name. Source installation also installs
the MIME declaration and refreshes available caches. No Windows icon adapter
is claimed. GNOME folders remain directories rather than Finder-style packages.

Primary platform references:

- https://specifications.freedesktop.org/shared-mime-info/latest-single/
- https://discourse.gnome.org/t/survey-custom-file-thumbnail-usage/18241/3
  (GNOME Files maintainer describes metadata::custom-icon-name and icon themes)

Linux/GVFS behavior is designed against those contracts but not locally
executed on this macOS host. No Icon-CR file, resource fork, .directory file or
absolute icon path is introduced into portable ball contents.

## Portability fix

The saved runtime.build is an absolute host path. ZIP export/import already
removed it, but raw directory moves kept it and both frontends trusted it.
`effective-runtime` is now a read-only native action before ball loading. It
chooses an executable matching the host format/CPU from the saved build or
the receiving default. `ball/platform.c` bounds header reads and recognizes
Mach-O/universal slices, Linux ELF and presumptive Windows PE. POSIX executable
permission is checked. This is header/host compatibility, not a guarantee that
every transitive system dependency is installed. The macOS build property and
both GNOME runtime-selection sites use the action without manifest mutation.

`loader/CMakeLists.txt` generates runtime-capabilities.json from the actual
worker backend selected by the build. Normal macOS/GNOME packaging retains it.
`ball/runtime_selection.h` resolves the saved backend in launch preparation:
native stays native when supported, otherwise a saved preference can use
declared Unicorn; requested Unicorn must be available. Explicit CLI overrides
do not silently switch backends. Malformed declarations and unavailable
backends produce actionable errors. Legacy workers without declarations keep
their existing supported-host/default/startup checks; a native→Unicorn fallback
is not guessed from the file format alone.

Runtime Undo uses a dedicated restore-runtime data action. This can restore a
missing, foreign or empty prior override exactly, while new runtime-build choices
keep their existing path validation. Both native frontend inverse constructors
use it, including no-build reset handling. Effective selection remains read-only.

Cache paths are OS2/DLL/2INE/<backend>/<first16-runtime-SHA256>, not literal
architecture directories. The identity hashes sorted loader/module filenames
and contents plus the capability declaration. Different host binary headers
and libraries therefore produce different identities. Old caches can remain
with a moved ball but are not selected as the receiver's runtime. Fonts,
soundfonts and external provider assets are taken from the receiving build.
Default C: and saved applications are relative/guest paths; external mapped
media remain explicit external dependencies. Generated 2ine.cfg is rewritten
for the new root, while ball.json/CONFIG.SYS/profile/program bytes are retained.

## Validation

`tests/tools/test_ball_portability.py` verifies matching, missing, foreign,
nonexecutable and malformed overrides; backend capabilities and strict errors;
raw-directory relocation with unchanged unknown manifest fields/profile bytes;
guest paths and a fresh receiving-runtime fingerprint, with old cache untouched.
Five cases pass against `build-ball-engine`, including foreign CPU, bounded
universal-binary slices and missing/foreign/empty runtime Undo restoration.

`tests/integration/ball_relocation.py`, registered as ball_relocation CTest,
creates an original LX guest, saves an unavailable foreign runtime and native
backend, moves the ball to a path containing spaces/Unicode, leaves the old
location absent and foreign cache untouched, then launches through the receiving
runtime. The guest verifies C: and its TEMP working directory and exits42.
Manifest, CONFIG.SYS, profile and installed guest bytes must remain identical.
The native-configured Mac→Unicorn result is recorded in
`/tmp/2ine-portable-native-move.log`. Work files are retained in a unique run
under `build-video-review/ball-relocation-tests`.

Final Swift source/model checks use workspace-local caches and pass12 tests in
`/tmp/2ine-portable-undo-swift.log`. Final native-configured moved-ball CTest
passes in0.94seconds (`/tmp/2ine-portable-native-move.log`). Relevant Python tool
tests pass17 cases in3.139seconds (`/tmp/2ine-portable-handoff-tests.log`), with
TWOINE_BALL_LIBRARY selecting the rebuilt engine and TWOINE_BUILD selecting
the valid build-video-review worker. The checkout's older default build/
contains an unrelated broken compiler-DLL symlink; using that as the test's
runtime input causes an existing hash error, not a portability failure.
Ruff, ty, source boundaries, binding consistency and whitespace checks pass.
All ten ICNS sizes, matching release/development document declarations, and an
isolated GNOME source-install layout were independently checked. No Linux/Windows runtime
execution, Linux desktop icon display or installed Finder cache refresh is
asserted by these macOS tests.
