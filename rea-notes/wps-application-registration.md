# Workplace Shell program registrations and the application list

The actual Documents/Lotus.2ineball was inspected read-only on 2026-11-04.
Its manifest had an empty applications object, while OS2/OBJECTS.INI contained
36 persistent WPS objects, including 26 WPProgram registrations. The original
installer therefore registered programs through the existing WPS backend;
the ball manager had no reconciliation step. No executable scan was needed.
Raw record evidence, the source profile hash and original path are retained in
`build-wps-current/actual-object-records.json`. Selected profile SHA-256:
`ebce8c880813c5a0a868e7d22bfb0615c7ebe05fa52df0a0d3cf13f952500f84`.

For example, handle 00020017 registers the title `Lotus Word Pro^for OS/2 Warp 4`,
object ID `<WordPro97OS2EN>`, executable C:\LOTUSW4\WORDPRO\WORDPRO.EXE,
startup directory C:\LOTUSW4\WORDPRO and ICONFILE ending LWP95APP.ICO.
The other main programs have corresponding real WPProgram records. Folder
records identify the suite/accessory/help hierarchy; a PDF WPShadow is not a
program launcher. Carets/line breaks in titles become spaces in the flat native
application list while folder provenance is retained.

## Integration and ownership

The shared ball engine reads the persistent records written by PMWP's actual
WinCreateObject/WinSetObjectData/WinDestroyObject implementation. Snapshot,
prepare-launch and post-install receipt paths reconcile WPProgram registrations.
Snapshot refresh therefore also repairs older already-installed balls. The
profile remains the authoritative object state; the manager does not write it.
This adapter does not decode every original IBM binary WPS abstract-object blob
or implement arbitrary SOM-derived WPS classes.

Generated launchers retain EXENAME, STARTUPDIR, PARAMETERS, title, source folder
hierarchy, object handle/ID and source-record SHA-256. Bare executables/modules
are resolved against the registered directory and guest PATH/LIBPATH. Parameters
retain quoted arguments and caret-escaped setup delimiters. Missing executables
remain registered; registration is not a claim that a target can run.

`wpsRegistration` marks ownership separately from manually added launchers.
Object changes update managed launchers; object deletion removes only managed
records. Friendly-name collisions preserve a manual launcher and give the WPS
record a stable handle suffix. Explicit edits detach management and preserve its
icon. User removal suppresses rediscovery of that object; Undo restores it.
Unrelated manual launchers survive changes and uninstallation. Folder objects
are provenance rather than fake executables; programs outside the reachable
desktop-folder tree are skipped with an explanation. Corrupt/oversized profiles
preserve existing launchers and return a warning. Missing object profiles are a
normal empty state and produce no registration log or manifest write.

PMWP now applies a setup TITLE on creation and WinSetObjectData to its persisted
title field as well as setup data. Its existing native regression verifies this
by checking normal duplicate-title lookup after a rename.

## Icons

Registered ICONFILE takes precedence when readable; ICONRESOURCE selects the
specified icon resource ID in the supplied DLL rather than its first icon.
The existing executable icon path remains the fallback. Both native frontends
receive the established RGBA/width/height result shape. Explicit manual icon
changes supersede the registered source; ordinary name/arguments/cwd edits keep
the valid registered icon.

The actual Approach object points to C:\LOTUSW4\APPROACH.ICO, which is absent.
The identical declared filename is installed at
C:\LOTUSW4\APPROACH\approach.ico and exists on the original application ISO at
ENGLISH/APPROACH/APPROACH.ICO (766 bytes). A narrow generic fallback tries only
that same filename beside the registered executable, and accepts it only after
successful decoding. It records the declared path, actual path and reason in
the launcher/report and icon response. There is no directory-wide icon search
or guessed substitute image.

This original icon is a Windows ICO directory with an uncompressed 4-bit DIB,
not an OS/2 IC/CI/BA file. The shared bitmap decoder now handles bounded
uncompressed 1/4/8/24-bit Windows ICO DIBs with checked palette/XOR/AND extents.
PNG-compressed entries, 32-bit-alpha ICOs and unsupported compression remain
outside this addition. Existing OS/2 formats use the same established pipeline.

In a fresh private copy, all five main programs return genuine 32×32 RGBA icons:
Word Pro uses its supplied OS/2 ICO; Approach uses the original Windows ICO;
Freelance, 1-2-3 and Organizer use their embedded executable resources.
`build-wps-current/actual-copy/core-apps-icons-final.json` records each actual
application and icon provenance. Source Documents data was never changed.

## Reports, contracts and verification

Each changed object profile or suppression policy writes a compact summary and
per-program status to logs/wps-registration-latest.json. It records added,
updated, removed and skipped counts with IDs, paths, source hashes, folder and
icon provenance. The snapshot and installer receipt expose its summary/path.
The native Logs UI can open this report. All GTK visible Activity labels now
say Logs while internal page IDs remain `activity` for preference compatibility.

IBM's *OS/2 Workplace Shell Configuration Techniques*, GG24-4201-00,
particularly the program setup tables and ICONRESOURCE description, supplies
the normal setup-string contract. ICONRESOURCE uses an ID and module; object
IDs survive rename/move. [IBM publication, archived PDF](https://web-docs.gsi.de/~kraemer/COLLECTION/OS2/gg244201.pdf).
The local Toolkit PMWP declarations establish ordinals 281/250/251/252, and the
actual installer records establish the selected Lotus settings. This run did
not capture a native-call trace of the installer; persisted original registration
data and independent API regressions are the evidence used here.

Focused tests: 20/20 WPS and ball-engine cases pass (6.867 seconds), including
creation/update/delete, folders, manual collisions/edits, hide/Undo, explicit
ICONFILE, exact ICONRESOURCE selection, same-filename fallback, original-name
preservation, unsupported/truncated/oversized icon bounds, missing/corrupt profiles
and report refresh for skip-only changes. Logs:
`/tmp/2ine-wps-all-icon-tests.log`, `/tmp/2ine-wps-all-icon-build.log`.
Ruff, ty, clang-format and diff checks pass. The root agent owns unified runtime,
full tool/Swift regressions, actual-ball refresh and release installation.

The root integration run refreshed the actual Documents ball after those checks:
26 managed registrations are persisted, and all five main-app icon responses
contain valid32×32 RGBA payloads. Evidence is retained in
build-macos/current/lotus-live-verification.json. Full tool tests pass199/199;
the receiving runtime suite passes66/66, with the four affected installer/ball
integration cases repeated successfully after the final icon change.
