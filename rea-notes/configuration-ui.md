# Ball configuration and display DPI

## Scope and storage authority

This is tooling for the existing 2ine formats, not a reconstructed OS/2 binary.
The source authority is `os2libs/pmshapi/profiles.c` and
`os2libs/pmwinx/registry.c`: user/system profiles accept native linked OS/2 INI
records or `2INEPRF1` records (LE32 app/key/data lengths with Latin-1 NUL-terminated
names and exact binary data). The registry is a separate profile at
`C:\OS2\REGISTRY.INI`; each application name is a lowercase root/key path;
`v:name` is LE32 registry type plus exact data; `k:`, `c:`, `t:` hold key metadata.
Native INI saving uses the same `2INEPRF1` format as the runtime. Unedited value
bytes and case-sensitive profile names survive conversion. Unknown registry
types remain available as hexadecimal. This does not import Windows hive files.

`ball/configuration.h` adds `configuration-read` (`name`: user/system/registry)
and `configuration-write` (`name`, `path`: application/key path, `key`, `text`:
hexadecimal bytes, `expected`: SHA256 revision, optional `remove`). Reads report
path, format, revision and raw entries. User/system paths honor effective
USER_INI/SYSTEM_INI from CONFIG.SYS and explicit environment overrides. Writes
reject malformed/unsupported databases and stale revisions, validate DWORD
extent, normalize registry ASCII names, and create parent key metadata. Native
INI cycle/bounds validation mirrors the runtime reader. Size limit: 32 MiB.
The revision guard checks the current whole file; atomic replacement prevents a
partial file. As with existing CONFIG.SYS saving, an independent non-cooperating
writer can still race between checking and replacement. The desktop serializes
its own edits and requires the guest stopped for profile/registry changes because
the runtime caches open profiles. Stored entries are shown; intrinsic 2ine
printer-default overlays are not materialized into the database by inspection.

## Desktop behavior

macOS top-level Configuration retains Cmd-4. Inner native tabs are CONFIG.SYS,
User Profile, System Profile, Registry, Display, Environment & Audio. The value
sheet supports exact hex, Latin-1 with explicit NUL termination, and little-endian
DWORD. Add/edit/delete operations carry the file revision. Failed saves leave the
sheet and its draft intact. Deletion requires a product confirmation.

A one-second per-ball poll reads only CONFIG.SYS while the owned guest runs.
Clean text follows disk; unsaved drafts retain their original base and surface a
conflict when disk changes. A reverted clean draft is dropped so later external
edits appear. Save uses the existing Latin-1/CRLF-preserving revision guard.
Polling does not repeatedly reload icons or write desktop preferences. Read
errors appear in the editor. The guest can continue while CONFIG.SYS is viewed,
edited or saved; changed SET/LIBPATH values apply at next launch.

GTK uses an inner StackSwitcher for CONFIG.SYS/user/system/registry/display,
with environment/audio still available under Settings. Native Adw dialogs edit
the same raw values through the C actions. A one-second running-child poll updates
CONFIG.SYS and preserves conflicting drafts. The packaging script includes the
new `gnome_configuration.py` module. GTK runtime UI is **not validated here**:
`python3 -c 'import gi'` fails with ModuleNotFoundError, and no Linux GTK VM is
configured. A persistent integration script is provided for that environment.

## DPI contract

`system.display_dpi` is per-ball, whole-number 72–384; default 96. Desktop presets
are 96/120/144/192. Other valid values are visible as Custom and can be set through
Settings/CLI. Settings take effect at next application launch. Launch plans carry
validated `TWOINE_DISPLAY_DPI`; runtime config parsing also supports
`system.display_dpi` and rejects invalid environment overrides. DPI remains
separate from physical host window pixels and from host Retina backing scale.

Display DC capabilities, Open32 LOGPIXELS/physical dimensions, default and
point-based fonts, font metrics, physical presentation units, WinMapDlgPoints,
control system metrics, menu rows and frame menu layout use the shared display
DPI. Explicit pixel font/bitmap/window coordinates retain their pixel contract;
application code still needs to honor the DPI/layout APIs. PDF/PCL printer DCs
remain A4/300 DPI. No claim is made that every original application's custom
hardcoded pixel layout responds to a DPI setting.

## Acceptance evidence (2026-11-03)

- `cmake -S . -B build-image-app-runtime -DTWOINE_FFMPEG_PROVIDER=system -DTWOINE_MIDI_EXTENDED=OFF`
- `cmake --build build-image-app-runtime --target twoine_ball twoine_display_dpi_tests twoine_pm_tests twoine_dialog_tests twoine_printing_tests pmwinx lx_loader -j8`
- `ctest --test-dir build-image-app-runtime -R 'display_dpi_|printer_output|presentation_manager|pm_dialogs' --output-on-failure`: 7/7 passed. Tests cover DPI96/120/144/192 caps/font metrics/dialog units/physical inch mapping and printer300, with retained PM/dialog/real PDF/PCL guest regressions. Log `/tmp/2ine-display-tests.log`.
- `TWOINE_BALL_LIBRARY=$PWD/build-image-app-runtime/libtwoine_ball.dylib python3 -m unittest tests.tools.test_configuration tests.tools.test_ball_desktop -v`: 10/10 passed. Native INI parsing+preserving binary data, SHA256 parity, stale/corrupt rejection, registry metadata/type/delete, alternate profile path, display validation/launch env. Log `/tmp/2ine-configuration-c-tests.log`.
- `swift test --package-path macos --scratch-path build-macos/configuration-tests --disable-sandbox`: model16 + core10 passed. Includes an actual one-second running-state poll, external Latin-1/CRLF edit, preservation/conflict/reload, reverted draft, and refusal to write profiles while running. Log `/tmp/2ine-configuration-swift.log`.
- Ruff/py_compile passed for new/changed GTK Python and integration script; ty passed new C-engine Python tests. GTK-specific ty cannot resolve missing gi (same environment limitation), so this is not claimed as a full GTK typecheck.
- Native CUA smoke used unique `io.github.bitwisecook.2ine.ConfigurationReviewUnique`, bundle `build-macos/configuration-review/Configuration Review.app`, isolated `Config Review.2ineball`. Six inner tabs and clean top-level labels visible. Registry DWORD42 displayed, native editing sheet switched hex→decimal and saved43; refreshed row showed43. Display showed persisted144DPI150% and next-launch qualification. Screenshots visually inspected through CUA. Review app quit. No real user ball or OS/2 software licence was modified/accepted. After this smoke, explicit field labels were added and the final Swift suite rerun.

Linux follow-up acceptance:
`xvfb-run -a dbus-run-session -- python3 tests/integration/gnome_configuration.py --build build-portable`
plus existing `gnome_features.py`/`gnome_ball.py`, and package generation/import.
The configuration script owns an isolated sleeper to verify live child polling;
it does not claim that sleeper is an original installer.

## Typed registry tree (2026-11-03)

The Registry settings page now has a native hive/key/subkey outline and a value
table, rather than exposing registry metadata as generic INI rows. Empty keys and
their ancestors remain visible; `k:`, `c:` and `t:` metadata is hidden. Four Open32
hives are supported. Unknown persisted roots are inspectable without silently
moving or normalizing them. Existing noncanonical names remain read-only.

`BallCore/Registry.swift` derives its contract from `pmwinx/registry.c`: ASCII-only
case folding, Latin-1 names/data, a 32767-byte key-path limit, a 32765-byte value
name limit (the native `v:` prefix consumes two bytes), and a 16 MiB value payload.
Known type names cover codes 0–11 and custom UInt32 type codes remain supported.
Strings, MULTI_SZ, LE/BE DWORD and QWORD have checked typed editors; unknown,
malformed or unrepresentable payloads stay exact hexadecimal bytes. Registry
strings are not expanded by the editor. Incomplete type headers are editable only
as explicit full-record hex repairs. Switching representation fails without
changing a draft when conversion would lose bytes. Native REG_DWORD requires four
payload bytes; other opaque native types are not silently coerced.

New values can create parent keys through the existing backend; value deletion is
supported. No unsupported native RegDeleteKey operation is invented. Saves retain
the full-file revision guard and stopped-guest requirement. A conflict preserves
the editor draft. The shared C engine preserves all unrelated opaque records.

`swift test --package-path macos --scratch-path build-macos/registry-tests
--disable-sandbox` passed 16 model +20 core tests, including eight registry tests.
The native integration test writes an isolated binary profile, updates a DWORD,
checks malformed/unknown record byte preservation, then rejects a stale revision
without changing the file. Log `/tmp/2ine-registry-swift-final-tests.log`.
An isolated 28-record UI fixture is available at
`build-macos/registry-review/Registry Review.2ineball`; the final Swift view builds.
The new tree's visual acceptance is being performed by the parent agent in its
combined Settings acceptance app, separately from the earlier generic editor
smoke described above. GTK tree implementation is coordinated with the GNOME
agent; no Linux GTK runtime is available locally for a visible acceptance claim.
