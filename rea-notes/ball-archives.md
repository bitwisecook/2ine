# Ball ZIP transfer

Bounded tooling change: ZIP import/export through the shared C ball API, macOS
and GNOME controls, optional media bundling, and exported engine/runtime build
identity. Acceptance requires a byte-preserving private-file round trip, usable
bundled image mappings after the original sources are removed, ejected media
when omitted, non-replacing imports, rejection/cleanup of malformed archives,
and an independently verified runtime fingerprint.

Source base: 7867408d05983f7ddce92f6c68c34852fd43ea03 with existing uncommitted
disk-image, runtime and native-manager changes retained. This is host tooling;
no original OS/2 ABI or reconstruction gate is changed. The existing JSON C ABI
stays at version 1 with additive request actions/fields. ZIP streams use the
system libarchive ABI; BSD API headers/license are pinned to upstream v3.7.4.

Validation on Apple Silicon, macOS 27.2, Swift 6.4.2, AppleClang 21:

- `cmake -S ball -B build-ball-engine -DCMAKE_BUILD_TYPE=Debug` and
  `cmake --build build-ball-engine --parallel 2`: pass.
- `TWOINE_BUILD="$PWD/build-ball-engine" TWOINE_BALL_LIBRARY="$PWD/build-ball-engine/libtwoine_ball.dylib" python3 -m unittest tests.tools.test_ball_archives tests.tools.test_ball_engine tests.tools.test_ball tests.tools.test_ball_desktop`:
  39 tests pass. The build override avoids the unrelated broken Linux
  `build/clbr20.dll` symlink in this checkout.
- `CLANG_MODULE_CACHE_PATH=/tmp/2ine-archive-module-cache swift test --package-path macos --disable-sandbox --cache-path /tmp/2ine-archive-spm-cache --config-path /tmp/2ine-archive-spm-config --security-path /tmp/2ine-archive-spm-security`:
  strict build and 18 Swift tests pass, including model import success and
  failed-import selection preservation.
- Ruff check/format and ty on changed Python code: pass. Pinned clang-format
  21.1.8 check on the new archive header and platform header: pass.
- Native UI inspection: Import Ball toolbar control opens the ZIP-only chooser;
  Export Ball opens its save panel with Include disk images initially off and
  toggleable. Both panels cancel without exporting user files.

The first disposable UI launch from the restricted command sandbox aborted
inside HIServices application registration, before the app startup code. The
same executable launched outside that sandbox and exposed both archive panels.
No archive operation appears in the crash stack. This observation does not
establish a general fix for unrelated application startup failures.

Windows and GNOME runtime execution remain unverified on this macOS host. CI
installs libarchive on Linux/Windows and runs the archive suite. External folder paths remain reconnect hints; unavailable folders and images
are ejected. Internal directories are normalized relative to the ball, and
source-machine runtime selections/cached binaries are omitted. Old runtimes may
lack source-version records; exported content fingerprints identify their bytes
without attributing the exporter's Git revision to them. New CMake builds emit
2ine-build.json and macOS runtime bundles carry it. The archive format and
commands are documented in ball/README.md and docs/ball.md.


Path portability and reconnect acceptance: 44 Python tests and 19 Swift tests
pass after adding internal absolute-path rebasing, legacy Windows separators,
foreign-host paths, retained guest working directories/launchers, disconnected
library search paths, automatic reconnection on refresh and mountpoint changes.
The prior undo-failure test now verifies that restoring a missing folder creates
an ejected drive and remains redoable, as required by the new behavior.

Native UI verification used an isolated Remap Test bundle and preferences under
/tmp/2ine-remap-ui. E: displayed Ejected with its previous host path and saved
E:\APPS working directory. Change Mountpoint preselected E:, accepted the
replacement folder, and immediately showed E:\APPS in the drive browser. C:
remains a required private directory. The snapshot unavailableDrives projection
is transient; the original mapping stays in ball.json for reconnect/undo. UI
runtime validation for GNOME and Windows remains unverified on this host.
