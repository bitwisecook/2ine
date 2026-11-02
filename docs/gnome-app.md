# GNOME ball manager

2ine Balls uses GTK 4, libadwaita and PyGObject. GNOME 51 is the design target;
GTK 4.18+ and libadwaita 1.9+ are the supported library floor, including Ubuntu
26.04. The shared C engine owns ball persistence, drive translation, runtime
selection, CONFIG.SYS validation, launch plans and installer receipts. The
frontend calls that engine on one serialized worker and updates GTK only on
the main loop. No Python subprocess or shell mediates application launches.

## Run from a checkout

```sh
cmake -S ball -B build-ball-engine -G Ninja
cmake --build build-ball-engine
python3 tools/gnome_balls.py --build build-portable
```

The full runtime must be built separately as described in the main README.
Install `python3-gi`, `gir1.2-gtk-4.0` and `gir1.2-adw-1` on Ubuntu.
`python3 tools/install_gnome.py` installs a development launcher tied to the
checkout. Distribution packages use a separate relocatable launcher.

## Native workflow

Create a ball with Ctrl+N or open a directory with Ctrl+O or a file-manager
drop. New directories have the `.2ineball` suffix. Existing directories with
`ball.json` also open. The sidebar remembers external balls and the last
selection. A narrow window becomes a navigation view with a Back button.
Drives is the initial workspace. The five section buttons show labels where space allows and icons with
native tooltips at smaller sizes. Alt+1 through Alt+5 select Drives,
Applications, Settings, CONFIG.SYS and Activity.

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
confirms discarding a draft. Closing or Ctrl+Q offers Save, Discard Changes
or Cancel when drafts remain. Ball writes and launch preparation disable
competing operations while leaving navigation, activity and output usable.
Close guest windows before closing the manager during a running application.
Activity also provides Stop Operation (Ctrl+Period), a native confirmation, and
Force Stop after termination was requested. Both retain the actual exit/signal
and output before releasing the operation.

Activity shows the actual process exit or signal, last installer receipt and
the last 64 KiB of output; complete logs remain in `logs/gnome-*.log`. An
installer exit does not establish that every installed application works.
Profiles, registry and shell objects persist in the common engine's formats;
binary state has no editor in this frontend. The manager does not provide an
interactive stdin terminal.

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

Build a complete portable Linux runtime first. The package builder copies only
declared frontend files, runtime modules, Liberation fonts and soundfont assets,
closes private ELF dependencies, and replaces build-directory RPATHs with
`$ORIGIN`. Distribution libc, desktop and graphics drivers stay external.
Original OS/2 files, private fonts and application media are not packaged.
The package includes third-party license notices and runtime input hashes.

```sh
python3 tools/build_gnome_packages.py --build build-portable --output build-gnome/packages
```

This produces `2ine-balls_0.2.0_amd64.deb` and
`2ine-balls-0.2.0-1.x86_64.rpm` on x86-64. Choose `--format deb` or
`--format rpm` for one format. Builders require `patchelf`, `binutils`, `ldd`
and the selected format's `dpkg-deb` or `rpmbuild`. Use a fresh output directory;
an existing staged 2ine installation is rejected. Other parts may populate the
prefix first, as Snapcraft does with Python dependencies. Build on each target
architecture and distribution baseline. Debian's libc floor is derived from
the selected ELF symbols; RPM retains system ABI requirements and excludes
bundled modules from public providers and external requirements.

Install using `sudo apt install ./2ine-balls_*.deb` or
`sudo dnf install ./2ine-balls-*.rpm`. The installed launcher locates its shared
engine and bundled runtime relative to its installation prefix. A ball can
still select an external build without replacing the packaged engine.

Ubuntu's cross-distribution format is **Snap**. The strict `core26` recipe in
[`../snap/snapcraft.yaml`](../snap/snapcraft.yaml) uses the official GNOME
extension, builds the pinned SDL provider, and stages the same app/runtime.
With Snapcraft 9.2+, snapd and an initialized LXD builder:

```sh
snapcraft pack --use-lxd --platform amd64 --output build-gnome-snap
sudo snap install --dangerous build-gnome-snap/2ine-balls_*.snap
sudo snap connect 2ine-balls:removable-media
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
[James Doyle's GNOME Blueprint skill](https://gist.github.com/james2doyle/20a45c2d3f853a1d6114d7bad7b2fafb),
which links GTK Workbench examples and native adaptive patterns. The static
window is GtkBuilder XML; dynamic lists use standard Adwaita rows and groups.
There is no custom application theme. Icon buttons have tooltips and accessible
names; native controls supply keyboard and screen-reader roles.

The editable 128 px application SVG follows GNOME's simple shapes, shallow
extrusion and restrained shading. A warm ball of twine forms the O while one
loose strand draws S/2. It adapts the Mac icon's idea without a macOS tile or
glass treatment. The 16 px symbolic SVG simplifies this to a ball with a short
loose end. Both are installed into the hicolor icon theme.

## Verification

```sh
python3 -m unittest discover -s tests/tools
python3 tools/lxapigen.py --check
desktop-file-validate tools/gnome/*.desktop
appstreamcli validate --no-net tools/gnome/*.metainfo.xml
xvfb-run -a dbus-run-session -- python3 tests/integration/gnome_balls.py --build build-portable
xvfb-run -a dbus-run-session -- python3 tests/integration/gnome_features.py --build build-portable
dbus-run-session -- xvfb-run -a python3 tests/integration/gnome_package.py build-gnome/packages/2ine-balls_0.2.0_amd64.deb
```

The GTK integration executes a generated LX installer and saved launcher using
the real runtime. It checks cwd/arguments, settings, actual exit and receipts,
undo, config draft retention, conflict protection and narrow navigation. The
package test extracts to a path with spaces, audits private ELF relocation and
runs those workflows from the extracted frontend and runtime. CI adds a Fedora
45 / GNOME 51 library run and the strict Snap build. These checks do not qualify
a full SmartSuite reinstall or every installed suite application.


## Screenshot gallery

`tests/integration/gnome_screenshots.py` captures the actual GTK application,
not mockups, in both appearances. It covers the five views, installation media,
application tabs, drive/program pickers, creation/protection/export dialogs and
the adaptive layout. It writes PNGs, an HTML gallery, a JSON index and a ZIP:

```sh
xvfb-run -a -s '-screen 0 1440x1000x24' dbus-run-session -- \
  python3 tests/integration/gnome_screenshots.py --build build-portable \
  --ball '/path/to/preview.2ineball' --output build-gnome/screenshots
```

Use a separate preview copy when selecting real application media/state. Original
applications and media are never included in the distributable manager packages.
