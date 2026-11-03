# GNOME 0.3 native parity

The source baseline is published main c8ea1a1 (2026-11-03), including the Mac
0.3 release, original CMD/REXX services, OS/2 media/dependency recovery,
configuration/registry editors, runtime features, printing/video and ball
lifecycle changes. The work is host extension code, not reconstructed OS/2
source. No reconstruction milestone gate changes.

Acceptance is current shared-backend functionality through native GTK and
libadwaita workflows, a qualified portable Linux runtime and self-contained
Debian/RPM/Snap frontend payloads. macOS SDK/SwiftUI validation stays pending
at the user's explicit earlier request. Mac source was used as the feature
inventory, not as a layout specification.

## Feature comparison and implementation

The incoming GNOME frontend already contained the Mac's media recovery,
configuration/registry, runtime feature and printing counterparts. Source review
covered TwoineBall commands, ConfigurationView, SettingsPages, RegistryView,
OS2Support, BallManagement, Editors, DriveBrowser and BrowserSearch, plus the
common C request contracts. The user-facing matrix is in docs/gnome-app.md.

The missing host preferences now use Adw.PreferencesDialog: default new-ball
folder, desktop text-size inheritance and optional Activity font size. Existing
unknown preference keys survive saves. Settings uses a searchable boxed list
and Adw.NavigationView with native Back navigation instead of six horizontal
tabs. Registry keys use Adw.OverlaySplitView and a local breakpoint; file tables
keep their name column and restore size/date columns on wider layouts.

Applications uses searchable boxed rows, native selection/activation, keyboard
launch/edit, per-row menus and program-path copying. A native ShortcutsDialog
shows actual action accelerators; Ctrl+F targets the current mapped search.
Primary menus group ball, runtime/media and drive actions and remain accessible
in collapsed navigation. Native input dialogs preserve new/rename entries on
backend failure, expose an inline error and own the pending operation.

Missing balls now have an unavailable state, guarded mutation actions and a
list-only removal command. Its worker verifies the path is still missing;
removing the reference never removes a relocated package. A generated fixture
checks that its moved files remain intact.

Runtime resolution and dependency-audit log reads were moved to the serialized
worker. Ball settings, registry/files, history and recovery rules still come
from the common backend. Existing live CONFIG.SYS conflicts, unknown metadata,
media changes and installer receipt ownership are retained.

## Linux runtime and printing findings

The pulled runtime could not compile on Linux: runtime/features.h shadowed
glibc's features.h through the runtime include directory. It is now named
runtime/runtime_features.h with its references updated. A legacy test needed
strings.h for strcasecmp. The native i386 target selected host x86_64 zlib;
the compiler's -m32 library lookup now selects the target library. GCC CI also
found an unproven snprintf bound in shared-slot name copying and a temporary
owner value across pthread cleanup machinery. Explicit validated-length copying
and direct assignment of the atomic owner result retain behavior without
disabling warnings. Static
FFmpeg codec tables referenced by PC-relative assembly must bind locally in
ELF modules; the private static-library exports are excluded on Linux.

The new PMStars check counted the colored OS/2 title bar as stars. Actual
dots.png showed dot mode working with a teal caption; the check now samples
the client interior of the 96-DPI fixture, retaining the real dot/streak bounds.

Native printing reserved 64 KiB automatic buffers on a guest stack. The
printer-output failure's libpmwin.so prologue probed an unmapped 0x1feac;
addr2line bound it to commit_output in pmwin/print_job.c. The spool copy,
printer enumeration and dialog response buffers now use checked heap storage
and cleanup. Native printer_output and gui_pmstars both pass after these fixes.

Debian/RPM declare Zenity and cups-client. Snap stages those helpers, declares
the print-only cups interface and provider, uses /var/cups/cups.sock and sets
TWOINE_CUPS_BINDIR to its own absolute helper directory. System clients remain
the default outside Snap. No shell interprets titles or printer names. A real
process-level fixture checks enumeration and PCL submission from a helper
directory containing spaces, with the captured argv and output verified.
RPM now owns the new MIME icon, which the incoming package manifest omitted.
Debian/RPM replacement metadata upgrades the older plural 2ine-balls package
without removing user ball data. Sources, reference versions and notices for both helpers were added to the
shared catalogue. Full physical-printer/service qualification remains separate.

Primary design/API references: GNOME HIG adaptive design/dialog guidance,
Adw.NavigationView, OverlaySplitView and ShortcutsDialog documentation, and
James Doyle's GNOME Blueprint skill. Printing socket/interface behavior was
checked against Canonical's cups-interface documentation, snapd cups.go and
test-snapd-cups-consumer recipe. Ubuntu package copyright records provide
Zenity/CUPS authors, source versions and license notices. CUPS retains its
Apache/GPL2/LGPL2 exception; helper programs keep their own licenses.

## Validation

Ubuntu 26.04.1: GTK 4.22.4, libadwaita 1.9.1, Python 3.14.4. Portable compiler
Clang 20.1.8; native i386 GCC 15.2. FFmpeg 9.0.2 is the pinned source build.
The canonical default package runtime is Unicorn. Generated LX, media and
instrument fixtures are original test data, not redistributed Lotus/IBM media.

Local: 181 tooling tests and all 75 portable CTest checks pass. Six GTK suites
cover management/guest launch, drive/app editors, audio, configuration/printing,
ball lifecycle and native parity. The new parity suite exercises search,
keyboard launch, effective capabilities, OS/2 media plan/extraction, library
audit, preferences persistence, preserved inputs, adaptive registry and missing
ball cleanup. Existing lifecycle tests now click actual native alert buttons;
emitting the response signal alone did not close the dialog. Package tests run
all six suites against their installed frontend rather than the source adapter.

The optional i386 worker's broader suite exposed additional failures in the
newly pulled legacy-session and invalid-buffer video/bitmap paths. They are
not qualified by the GNOME portable-runtime checks and are not recorded as
passing. The compiled default package runtime does not advertise i386 support.
Native printing and PMStars are separately verified; full i386 qualification
remains an explicit limitation.

Logs are /tmp/2ine-gnome03-*.log; owning runtime output is under build-portable
and build-native. Real GTK captures and deliverables are under
build-gnome/native03. Final package, CI and merge receipts will be appended when
completed. Complete corresponding-source release artifacts and local strict
Snap confinement remain separate unverified release gates from the earlier
license audit.
