# GNOME ball manager

The GNOME manager is a native Rust application built with gtk4-rs and
libadwaita-rs. Its windows and dialogs are Blueprint composite templates.
GNOME 51 is the design target; GTK 4.18+ and libadwaita 1.9+ are the supported
library floor, including Ubuntu 26.04. The crate uses Rust 1.99 and the 2024
edition, and lives in [`apps/gnome`](../apps/gnome) beside the macOS app.

The shared C engine owns ball persistence, drive translation, runtime
selection, CONFIG.SYS validation, launch plans and installer receipts. The
manager links it statically and calls it on one serialized worker thread, with
read-only requests on the GIO thread pool; GTK is updated only on the main loop.
Typed serde models describe snapshots without writing `ball.json`. No Python
interpreter, subprocess or shell mediates application launches.

## Run from a checkout

Install the Rust toolchain (1.99 or later), CMake, Ninja and the GTK
development packages. On Ubuntu:

```sh
sudo apt install libgtk-4-dev libadwaita-1-dev blueprint-compiler libarchive-dev zlib1g-dev
make gnome-run GNOME_RUNTIME_BUILD=build/runtime
```

`make gnome-run` builds the manager with Cargo and opens it with the given
runtime build; `cargo run --manifest-path apps/gnome/Cargo.toml -- --build DIR [BALL]`
is equivalent. Build output is kept in `build/gnome`. The full runtime is built
separately as described in the main README. Without `--build`, the manager uses
`TWOINE_BUILD`, then a packaged runtime beside the executable, then
`build/runtime`. The crate also builds `2ine-dialog`, the native libadwaita
helper the runtime uses for print destinations, save locations and failure
reports; packages install it beside `lx_loader`, and the manager passes its own
copy to the guests it launches through `TWOINE_DIALOG`.
`python3 apps/gnome/install.py` builds a release binary and
installs a development launcher, desktop entry, MIME type and icons tied to the
checkout.

## Native workflow

Create a ball with Ctrl+N or open a directory with Ctrl+O or a file-manager
drop. New directories have the `.2ineball` suffix. Existing directories with
`ball.json` also open. The sidebar remembers each external ball and the last
selection. A narrow window becomes a navigation view with a Back button.
Drives is the initial workspace for a new ball. The six sections — Drives,
Components, Install, Applications, Settings and Logs — use a header-bar view
switcher on wide windows and a bottom view-switcher bar on narrower ones, where
the header shows the ball name instead. Alt+1 through Alt+6 select them in order.
The header's Removable Media menu lists every CD, floppy and disk device with its
image or ejected source and lock state. It attaches/replaces and ejects images,
toggles floppy/disk write protection, creates an image for a device, browses
the drive, and adds a CD, floppy or disk device at the next conventional letter.
Sidebar rows show missing balls and how many applications are running; their
menu opens the folder, copies the path, renames or moves the ball to Trash.

Install Software (Ctrl+I), Run (Ctrl+Shift+R) and saved applications all use
structured argument arrays. Backslashes stay literal; quotes group spaces;
`""` preserves an empty argument. All three support per-drive working folders and environment overrides. The native
program picker browses mapped drives through the shared engine, including image
members; it does not assume those paths exist in the host filesystem. Add/Edit
Application has General, Working Folders and Environment views. Overrides inherit
independently, support explicit empty values and expand `%NAME%` in the core.
Preview Effective Environment shows the resulting guest values. Embedded OS/2
icons are decoded in the common backend; custom images are imported into the
ball. Rename, remove and Undo/Redo retain opaque application metadata.
Mapped host files and OS/2 paths are accepted by the common engine. Choose a
runtime under Settings; `runtime.build` persists per ball and is shared with
the Mac client. Unset overrides restore inheritance. Drive unmapping and
launcher removal keep installed files. Ball changes offer a native Undo toast,
Ctrl+Z and Ctrl+Shift+Z for Redo. History returns to the affected ball. Text
editors retain their own undo behavior. Beep-volume drafts stay with their ball
across unrelated changes until applied.

CONFIG.SYS drafts survive section changes, ball changes and refresh. Save
(Ctrl+S) compares the originally loaded text with disk contents. Conflicts
remain editable and never overwrite installer changes. Reload explicitly
confirms discarding a draft. Closing (Ctrl+W) or quitting (Ctrl+Q) offers Save, Discard Changes
or Cancel when drafts remain. Ball writes and launch preparation disable
competing operations while leaving navigation, activity and output usable.
Close guest windows before closing the manager during a running application.
Logs also provides Stop (Ctrl+Period), a native confirmation, and
Force Stop after termination was requested. Both retain the actual exit/signal
and output before releasing the operation.

Logs shows the actual process exit or signal, last installer receipt and
the last 64 KiB of output; complete logs remain in `logs/gnome-*.log`. Its
session list starts with Activity and Audits, followed by each application
session with its start time and state. The Logs menu opens the full log, the
dependency audit, the Workplace Shell application registrations
(`logs/wps-registration-latest.json`), the logs folder and the ball folder.
An installer exit does not establish that every installed application works.

Before starting a program the manager checks its OS/2 dependencies. When
original OS/2 media is required the launch is cancelled and Components opens
the first required group. A launch waiting for components or configuration is
shown in a banner on every section. After an installer started from an
attached image exits, a dialog offers Keep Attached or Eject; the drive letter
is retained either way. Launched guests receive their session and desktop name
(`TWOINE_DESKTOP_SESSION`, `TWOINE_DESKTOP_NAME`); shared startup services are
marked auxiliary and are asked to exit before being forced after two seconds.
Profiles, registry and shell objects persist in the common engine's formats;
Settings provides binary-safe OS/2 file editors and a Registry key tree. The manager
does not provide an interactive stdin terminal.

## Native settings and application preferences

Settings opens a searchable list of six sections with native Back navigation.
General holds runtime selection and diagnostics. Graphics & Sound combines
native/OS/2 window title bars, DPI, OpenGL providers, DIVE, video, PCM, MIDI,
PC-speaker volume and sound banks. Runtime availability and inherited defaults
come from the shared backend. Environment shows effective values and their
origins, Printing edits the inherited PDF/PCL queue, Config Files discovers
startup/profile files, and Registry presents a key tree and typed value editor.
On narrow windows the registry's Keys button opens an overlay; choosing a key
returns to its values. File tables keep names visible while secondary columns
return as space permits.

The main menu is available in both sidebar and detail navigation. Preferences
(Ctrl+,) selects a default location for new balls and an optional Activity log
text size. Text otherwise follows the desktop's settings. These application
preferences stay on the host and are separate from portable ball settings.
Keyboard Shortcuts (Ctrl+?) lists the actual window accelerators, including Export
Ball (Ctrl+Alt+E) and Add Drive (Ctrl+Shift+D); F1 opens help.
Ctrl+F focuses search in the current view or dialog. Applications supports
search by name or guest program, double-click/Return activation, Ctrl+Return to
launch the selected application, Ctrl+Shift+E to edit it, and a native action
menu with program-path copying and launcher removal.

## Functionality shared with the Mac app

| Function | GNOME interface |
|---|---|
| Open/create, rename, Trash and missing-ball cleanup | Sidebar, per-ball action menu and native dialogs |
| Runtime selection, inheritance and backend fallback | Settings → General |
| Guest directories, image media, file import, search/sort | Drives and native guest/media pickers |
| Saved applications, icons, arguments, cwd and environment | Applications and application editor |
| Original OS/2 file extraction and missing-library recovery | Applications → OS/2 Support and main menu |
| Display, graphics, video, audio and bank/channel mapping | Settings → Graphics & Sound |
| Effective environment and default printer | Settings → Environment / Printing |
| Binary-safe startup/profile files and registry values | Settings → Config Files / Registry |
| Portable archive transfer and opaque Undo/Redo | Main menu, native file dialogs and Undo toast |
| Actual exit/output, Stop/Force Stop, complete logs | Activity |
| Default ball folder and activity text size | Preferences |
| Source/version/download links and original-author credit | About 2ine → Legal |

Both clients use the same ball and backend contracts. Platform integration uses
GNOME file dialogs, Trash, clipboard, icon themes and system printing on Linux.

## Drives and portable transfers

Drives combines a drive-letter chooser, an OS/2 address entry, parent navigation
and native folder/file rows. Select a folder to browse it or a program to open
Run Program. Import Files, Import Folder, and file-manager drops copy into the
displayed folder; a drop on a folder row targets that folder. Existing files are
never replaced. Use as Default Folder sets the selected drive's persistent guest
directory. Per-application overrides leave that default unchanged.

Add Drive maps a folder or allocates a CD, floppy or disk device. Drives include
connected, ejected and unavailable entries, with source paths and protection
state. Drive Options reconnects folders, inserts/replaces images, ejects media,
changes write protection and locking, or removes the allocation. Existing media
can change while a guest runs; other conflicting writes remain serialized.

Create Disk formats HPFS, FAT32 or exFAT images; Create Floppy provides the
supported FAT12 capacities. Creation never overwrites a file. Images use 2ine's
internal filesystems with no host mounts or extracted tree. See the
[common-engine filesystem limits](../ball/README.md#image-backed-drives).
Installation's media section attaches original CD/floppy images and suggests
installers using bounded drive traversal. CMD/BAT installers require original
OS/2 CMD.EXE. A completed image-based installation offers an optional Eject toast.

Import Ball from ZIP chooses a new `.2ineball` destination. Export Ball presents
native archive options, including whether to include images, before choosing a
ZIP destination. Transfers use the core's portable format, provenance, runtime
reset, validation and unavailable-mapping behavior. Unsaved CONFIG.SYS drafts
must be saved first. Export does not embed external mapped folders or original
media unless image inclusion was explicitly selected.

## Packages

Build a complete portable Linux runtime and the release manager first. The
package builder copies the native frontend, runtime modules, Liberation fonts
and soundfont assets, closes private ELF dependencies, and replaces
build-directory RPATHs with `$ORIGIN`. GTK, libadwaita, libarchive, distribution
libc, desktop and graphics drivers stay external. Original OS/2 files, private
fonts and application media are not packaged. The package includes third-party
license notices and runtime input hashes.

```sh
make gnome-packages GNOME_RUNTIME_BUILD=build/runtime
```

This produces `2ine-ball_<version>_amd64.deb` and
`2ine-ball-<version>-1.x86_64.rpm` in `build/gnome/packages` on x86-64. Use
`apps/gnome/package.py --format deb` or `--format rpm` for one format.
Builders require `patchelf`, `binutils`, `ldd` and the selected format's
`dpkg-deb` or `rpmbuild`. Use a fresh output directory; an existing staged 2ine
installation is rejected. Other parts may populate the prefix first, as
Snapcraft does. Build on each target architecture and distribution baseline.
Debian's libc floor is derived from the selected ELF symbols; RPM retains system
ABI requirements and excludes bundled modules from public providers and
external requirements.

Install using `sudo apt install ./2ine-ball_*.deb` or
`sudo dnf install ./2ine-ball-*.rpm`. The installed `/usr/bin/2ine-ball` locates
its bundled runtime at `../lib/2ine-ball/runtime`. A ball can still select an
external build. A [Flatpak](flatpak.md) build is also provided.

The icon theme includes a standalone twine-ball MIME icon. After opening or
creating a ball, the frontend optionally assigns that themed icon name in
the local GVFS metadata database, preserving any existing custom icon. No
`.directory` file, image path or host icon resource is stored in the ball.
Headless/remote sessions without GVFS metadata keep the ordinary folder icon.
The source installer refreshes available MIME, desktop and icon caches.

Missing or foreign saved runtime overrides fall back to the receiving runtime.
Its `runtime-capabilities.json` determines the effective backend: an unavailable
saved native preference can use declared Unicorn support. Saved choices and
Undo records remain unchanged, and unavailable explicit requests fail clearly.

Ubuntu's cross-distribution format is **Snap**. The strict `core26` recipe in
[`../snap/snapcraft.yaml`](../snap/snapcraft.yaml) uses the official GNOME
extension, builds the pinned SDL provider, and stages the same app/runtime.
With Snapcraft 9.2+, snapd and an initialized LXD builder:

```sh
snapcraft pack --use-lxd --platform amd64 --output build/gnome/snap
sudo snap install --dangerous build/gnome/snap/2ine-ball_*.snap
sudo snap connect 2ine-ball:removable-media
```

Removable-media access is optional and must be connected to map media outside
Home. Strict confinement also limits hidden host directories and arbitrary
external runtimes; keep those inside accessible locations. The recipe declares
amd64 and arm64 builds; each must be built and tested on its own architecture.
Store publication, signatures and auto-connection approvals are separate from
creating a local package. Snap CI retains the built artifact without publishing.

## Design sources and icons

The layout follows the [GNOME HIG](https://developer.gnome.org/hig/),
[GNOME 51 developer notes](https://release.gnome.org/51/developers/) and
[James Doyle's GNOME Blueprint skill](https://gist.github.com/james2doyle/20a45c2d3f853a1d6114d7bad7b2fafb).
The window, dialogs and sections are Blueprint templates under
`apps/gnome/data/ui`, compiled into a GResource with the icons at build time.
Sections use standard Adwaita rows, groups, status pages and breakpoints, and
fit a 360 px window. A header-bar view switcher becomes a bottom switcher bar
on narrower windows. There is no custom application theme. Icon buttons have
tooltips and accessible names; native controls supply keyboard and
screen-reader roles.

The editable 128 px application SVG follows GNOME's simple shapes, shallow
extrusion and restrained shading. A warm ball of twine forms the O while one
loose strand draws S/2. It adapts the Mac icon's idea without a macOS tile or
glass treatment. The 16 px symbolic SVG simplifies this to a ball with a short
loose end. Both are installed into the hicolor icon theme.

## Verification

```sh
make gnome-test
TWOINE_TEST_BUILD=build/runtime xvfb-run -a dbus-run-session -- cargo test --manifest-path apps/gnome/Cargo.toml --test ui
desktop-file-validate apps/gnome/data/*.desktop
appstreamcli validate --no-net apps/gnome/data/*.metainfo.xml
make gnome-packages GNOME_RUNTIME_BUILD=build/runtime
xvfb-run -a dbus-run-session -- python3 tests/integration/apps/gnome/gnome_package.py build/gnome/packages/2ine-ball_*_amd64.deb
```

`make gnome-test` checks formatting, runs Clippy with warnings denied and runs
the unit tests. The `ui` test drives the real window through its actions and
widget tree under a display; with `TWOINE_TEST_BUILD` it also saves and launches a
real OS/2 program and checks its exit status and log. The package test extracts to a path with spaces,
audits private ELF relocation and starts the relocated frontend. CI adds a
Fedora 45 / GNOME 51 library run and the strict Snap build. These checks do not
qualify a full SmartSuite reinstall or every installed suite application.

## Sound banks and source credits

Settings → Graphics & Sound provides [sound-bank installation and 16-channel MIDI mapping](sound-banks.md).
About prominently credits original author Ryan C. Gordon and lists the
[project versions, authors, sources and downloads](project-sources.md).

Settings groups General, Graphics & Sound, Environment, Printing, Config Files and Registry
in a searchable list with native back navigation. General contains runtime and diagnostic preferences.
Environment shows CONFIG.SYS values, ball overrides and the backend-resolved
effective guest environment together; edit an override or reset it to inherit.
Config Files discovers CONFIG.SYS, User Profile (OS2.INI), System Profile
(OS2SYS.INI) and other INI files. Text files use revision-checked edits; binary
profiles edit individual application/key records; unsupported binaries stay
read-only. Registry has a key tree and typed values, preserving unknown types.
Profile dialogs edit individual values as exact hexadecimal, Latin-1 text, or
little-endian DWORDs. Registry values open in their type's own form — Latin-1
text for REG_SZ/REG_EXPAND_SZ, one string per line for REG_MULTI_SZ, and an
unsigned decimal or 0x number for REG_DWORD, REG_DWORD_BIG_ENDIAN and REG_QWORD —
with Hexadecimal Data and Complete Raw Record (type header plus payload)
alternatives. An existing value's type is shown read-only; names stored in a
non-normalized form can be inspected but not rewritten. Both check the file
revision before saving. Stop the guest
before editing binary profiles or the registry. CONFIG.SYS polls external changes
while the owned guest runs, retaining unsaved drafts and showing conflicts.
Display DPI is per ball and applies next launch; presets are 96/120/144/192, with
custom whole-number values 72–384 accepted by `system.display_dpi`.
Colour depth is per ball through `system.display_depth` (4, 8, 16 or 24, default
24). It sets the depth reported to OS/2 applications; 2ine still draws in true colour.
Title Bars selects Native, OS/2 or Both for top-level standard frames. Both is
the default; document windows retain their OS/2 controls in every mode. This
per-ball preference also applies next launch.
General also has Removable Devices: Lock Device to Image and Read Only per
CD, floppy and disk device. Locked devices cannot be ejected or replaced.
Graphics & Sound also selects OpenGL (receiving-runtime default, Mesa, Original
OS/2 or Off) and toggles video, PCM audio, MIDI synthesis and DIVE. Reset each
override to follow the receiving runtime. Capability and missing-support reasons
appear beside the saved choices; unavailable options remain selectable for a
future host, and apply on the next launch. Original OS/2 OpenGL requires the
user's renderer DLLs and remains unqualified. Installer audits retain the build
used for that launch even if the selected runtime later changes.
PC speaker volume (a percentage; the runtime default is 5%), sound-bank
installation and MIDI channel mapping are on this same tab. Printing chooses PDF or PCL through the OS/2 User Profile with revision
checks, and is blocked while a guest from the ball is running. Output is 300 DPI
on A4. A stored unknown queue remains visible and unchanged until explicitly
replaced; resetting the preference inherits the System Profile or virtual PDF
default. Merely opening Printing does not write a default.
