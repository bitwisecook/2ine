# Ball

A ball holds an application's OS/2 drives, configuration, installed files,
profiles, registry and saved launch commands. It uses the same loader and API
modules as an ordinary 2ine run. The CLI and GTK adapters use Python 3.11+ and the shared C ball library. The native Mac app is Python-free.

## macOS application

The native SwiftUI manager targets macOS 27+ and Swift 6.4 strict concurrency.
It builds as a self-contained `.app`, including the shared C ball engine and
complete 2ine runtime. Use `make macos-app` for Developer ID signing and notarization, or
`make macos-app-dev` for local development. See [macos-app.md](macos-app.md) for
prerequisites, credential parameters, tests and the `.2ineball` Finder format.

Right-click a ball in the sidebar, or use the Ball menu, to rename it or move it
to the Trash. Renaming preserves installed documents and pending CONFIG.SYS edits,
updates saved paths, and never replaces another package. A running guest must be
stopped first. On macOS, a trashed ball can be restored using Finder. Missing
packages can be removed from the sidebar without changing any files.

## GNOME application

The [native GTK 4/libadwaita app](gnome-app.md) manages each ball through the shared
C backend, including drives, per-ball runtimes, saved applications, settings,
conflict-aware CONFIG.SYS drafts, undo and live activity. It follows GNOME 51
patterns and uses a ball-of-twine OS/2 icon with a simplified symbolic variant.
The GNOME guide covers source launchers, relocatable Debian/RPM packages and
the strict core26 Snap recipe.

File/media browsing, component selection, registry keys/values and INI/config
file lists use a shared native tree/table control in each desktop frontend.
The control supplies disclosures, search, columns and optional mixed-state
checkboxes; each screen retains its own selection and editing rules. Mac rows
follow system light/dark appearance. File browsers retain their relevant-files
filter and sortable size/date columns.

The ball has separate **OS/2 Components** and **Application Installation**
tabs. The former installs original operating-system files; the latter attaches
application media and runs its installer. The last verified OS/2 media choice is
remembered in desktop preferences and suggested for new balls. A ball's own media
choice takes priority, and nested archive paths are retained without copying the
media into every ball.

Navigation stays visible in this order: Drives, OS/2 Components, Application
Installation, Applications, Settings, Logs. New balls start at Drives. Opening an
existing ball selects Applications when it has a valid launcher, Application
Installation when operating-system components are present, or OS/2 Components
otherwise. Refreshing metadata preserves the page you are using.

Component checkboxes apply immediately: tick to install, untick to remove.
Changes run in the background and can be queued while another row is working.
CMD and REXX are selected by default during first setup; an explicit later
removal stays removed. Only unchanged, owned files are removed, and uninstalling
does not require the original installation image. Modified or unowned files
remain protected.

The installer program field is an editable suggestions dropdown with a Browse
button. Root-directory installers are preferred; typing or browsing a different
program preserves that choice. Attached media provides Change Image and Eject
controls. A command-file launch missing CMD/REXX waits at the component pane and
continues after the required files have finished installing.

Guest Copy and Paste commands can use the [host clipboard](clipboard.md) for
text, rich text, HTML and bitmap images.

## Command line

Build the shared engine first with `cmake -S ball -B build-ball-engine` and
`cmake --build build-ball-engine`. The CLI and GTK adapters then use that same C library.


Create a ball and map extracted installation media:

```sh
python3 tools/ball/engine.py --ball "$PWD/Office.2ineball" init
python3 tools/ball/engine.py --ball "$PWD/Office.2ineball" map D /path/to/media
python3 tools/ball/engine.py --ball "$PWD/Office.2ineball" --build "$PWD/build" install 'D:\ENGLISH\INSTALL.EXE'
```

The installer is an original guest executable. `install` runs it and records its
process exit status in `logs/last-install.json`. That status is independent of
whether every installed application works. Installation media and applications
are supplied separately; 2ine does not bundle them.

Current macOS and Linux workers keep an installation operation open until its
guest child processes exit, including installers launched by a command script.
The dependency audit runs after that operation completes. A changed binary,
configuration or media inventory marks an earlier audit stale; launching an
application refreshes it and can recover missing OS/2 dependencies from remembered
media. Audit details remain in the ball's Logs.

Applications and Application Installation show compact advice when the library
check finds missing components. Click a component name to open OS/2 Components
with its row highlighted, then tick it to install. Following an advice link does
not tick the component. Libraries supplied by separate applications or database
clients remain listed separately, with a link to the full audit. An out-of-date
check offers **Check Libraries** before showing component advice.

The ball contains:

| Path | Purpose |
| --- | --- |
| `ball.json` | Drive mappings, environment overrides, runtime settings and saved commands |
| `2ine.cfg` | Loader configuration generated from the ball's mappings and settings |
| `drive_c/` | Private C: drive and installed application files |
| `drive_c/CONFIG.SYS` | Guest PATH, LIBPATH and SET variables, including changes made by installers |
| `drive_c/OS2/OS2.INI` | User profile |
| `drive_c/OS2/OS2SYS.INI` | System profile |
| `drive_c/OS2/REGISTRY.INI` | Open32 registry database |
| `drive_c/OS2/OBJECTS.INI` | Workplace Shell object and class metadata |
| `drive_c/OS2/DLL/2INE/` | Versioned loader and native API modules used by this ball |
| `drive_c/TEMP/` | Guest temporary files |
| `ipc/` | Ball-scoped queues, shared-memory backing and address assignments |
| `logs/` | Installation receipts |

Mapped directories retain their existing files. `init` preserves an existing
ball and refuses to initialize a nonempty unrelated directory. C: belongs to
the ball; additional drive letters can be mapped or unmapped explicitly.
Mappings are writable host directories, including media mappings. A ball is
an application environment, with host access available through mapped paths;
it is not a security sandbox. File extended attributes use the host filesystem's
xattr support and must be preserved when copying a ball that uses them.

Moving a `.2ineball` keeps its installed files, guest paths, profiles and saved
applications together. Native frontends use the receiving computer's runtime
when a saved runtime path is missing, is not executable, or belongs to another
host format/CPU. This resolution leaves the saved override and Undo state
unchanged. A saved native-backend preference falls back to the receiving
runtime's declared Unicorn backend when native execution is unavailable.
Explicit CLI backend overrides remain strict. Current workers carry
`runtime-capabilities.json`; an older worker without that declaration must not
be assumed to contain Unicorn. Unavailable requested backends produce an error.

Optional `startupPrograms` in `ball.json` contains up to 16 application records
with `program`, `arguments`, `cwd` and `environment`. The desktop and CLI launch
these services before the main program, with independent prepared environments,
and stop their own services when the main program exits or fails to start.
They do not recursively start more services. For original IBM SOM/DSOM programs,
the Lotus ball starts `C:\OS2\SOMDD.EXE` from `C:\OS2`, with the original
`C:\OS2\ETC\SOM.IR` prepended to its `SOMIR` environment. These files must first
be installed from the user's OS/2 media.

Runtime snapshots are disposable host caches selected by backend and a hash
of the loader, API modules and capability declaration. They do not become the
receiving computer's runtime merely because they were copied with a ball.
External mapped directories/media remain external and may need reconnection.
`2ine.cfg` is regenerated for the new absolute location at launch.

Finder gets the standalone twine-ball icon from the installed app's package
type declaration. GNOME installs the same vector in the icon theme and can
apply it to an opened ball using machine-local GVFS metadata. This adds no
custom icon file, resource fork or absolute icon URL inside the ball. A GNOME
ball remains a directory; metadata support depends on the desktop/session.

Configure settings and register an installed application:

```sh
python3 tools/ball/engine.py --ball Office.2ineball config system.beep_volume 0.03
python3 tools/ball/engine.py --ball Office.2ineball config runtime.backend unicorn
python3 tools/ball/engine.py --ball Office.2ineball config env.LANG en_US
python3 tools/ball/engine.py --ball Office.2ineball unset env.LANG
python3 tools/ball/engine.py --ball Office.2ineball add-app WordPro 'C:\LOTUSW4\WORDPRO\WORDPRO.EXE'
python3 tools/ball/engine.py --ball Office.2ineball --build build launch WordPro
python3 tools/ball/engine.py --ball Office.2ineball apps
python3 tools/ball/engine.py --ball Office.2ineball remove-app WordPro
python3 tools/ball/engine.py --ball Office.2ineball show
python3 tools/ball/engine.py --ball Office.2ineball unmap D
```

`system.trace_native`, `system.trace_events` and `system.beep_volume` are loader
settings. `runtime.backend` selects `native` or `unicorn`; `--backend` overrides
it for one command. Other application and graphics/audio environment settings
can be saved as `env.NAME`. The host's executable PATH stays intact; PATH and OS2PATH both configure the
guest search path. `%NAME%` expressions inherit defaults, so an application
override `PATH=%PATH%;C:\DOOM` appends the Doom directory. Installer changes to CONFIG.SYS
are read at each launch; explicit ball environment settings take precedence.
The default DPATH searches C:\OS2 and C:\OS2\SYSTEM for message files.
The default LANG is en_US, matching the current United States NLS provider;
CONFIG.SYS and explicit ball environment settings can override it.
Only SET and LIBPATH lines are interpreted. Kernel drivers and boot commands
in CONFIG.SYS are not executed by 2ine.

Workplace Shell metadata supports `WPFolder`, `WPProgram` and `WPShadow` records
under the virtual `<WP_DESKTOP>` root, stable object IDs, setup updates and deletion.
Shadows reference existing object IDs or mapped files. Class registration stores
case-sensitive class/module associations and supports deregistration. Records persist
between workers and stay inside the ball. Executing custom SOM classes, a Workplace
Shell desktop and host desktop shortcuts are not provided.

`run` and `install` accept a guest path or a host executable path inside a mapped
directory. Path lookup is case insensitive. The default working directory is
the ball's default for the executable's drive (the drive root until configured).
`install` starts in the installer's own folder unless explicitly overridden;
`run --cwd 'C:\WORK'` overrides it. Arguments after
the executable are passed through. Subprocesses inherit the ball's drive and
settings environment. Applications that require additional DLL directories can
add them to LIBPATH through CONFIG.SYS or `config env.LIBPATH`.

Without `--ball`, the manager uses TWOINE_BALL or the default ball under
`$XDG_DATA_HOME/2ine/ball/default` (`~/.local/share` when unset). `list` lists
each ball in that directory. `--build` defaults to TWOINE_BUILD or the checkout's
`build` directory. The raw loader supports an explicit TWOINE_CONFIG path; that
configuration replaces the system and user configuration files. It refuses an
unreadable explicit configuration or one with no accessible drive mappings.

Each launch copies the loader and native API modules from the selected build into a directory
identified by their content hash and execution backend. Guest module-name queries
can inspect those actual files. Existing versions remain available to running
processes. External provider assets still come from `--build`;
a ball alone is not a standalone runtime distribution.

## Lotus SmartSuite installation

The English SmartSuite base 1.7 installer uses OS/2's command processor and
system-version files. In Applications or Install Software, choose **Install
Components…**, then select your original OS/2 ISO or archive. The source is
remembered for that ball. Installed components are ticked and marked Installed;
the review distinguishes installed files from files to extract. CMD and original REXX support are the
default selection. Existing files and the application's CD mapping are preserved.
**Browse…** is optional when you need a particular folder or nested archive.

Components show files under their full guest destination folders, such as
`C:\` → `OS2` → `DLL`. Components supplied by the selected 2ine provider remain
visible but cannot be ticked; their settings link stays available. Choosing the
original OpenGL provider enables its media files. OpenGL Off also allows files
to be staged before enabling a provider. Printer and multimedia engines
currently use 2ine.

Expand a group to choose subgroups or individual files. Fonts are divided into
bitmap, TrueType, OpenType, PostScript and metrics; help separates its engine,
viewers, help books and reference books. Group checkboxes show a mixed state
when only some descendants are selected. Selecting a whole group includes its
declared requirements; individual file choices copy only the selected sources.
Each subgroup contains expandable destination folders such as
`C:\ → OS2 → DLL → font.ttf`; a folder checkbox selects its eligible descendants.
Groups using a 2ine provider are greyed out. Their **2ine** button opens and
highlights the matching setting. **Install OS/2 System Component** returns to
the component tab; choosing original OpenGL enables, ticks and highlights that
group. Native-only printing and video groups stay disabled, with their current
provider limits explained in Settings.

To add the original help manager, viewer and books, select **Full Help** in
**Install Components…** and click **Install**. It reuses the remembered media.
Damaged optional fonts are skipped with their paths and integrity errors listed
after installation; required support-file errors still stop extraction.

Optional groups also include **SOM/DSOM Object Runtime**, **Unicode and C
Libraries**, **Multimedia Files**, and **Networking Files**. SOM/DSOM includes
the original libraries, daemon/server programs, messages and repositories, and
also selects its Unicode and C libraries. It adds the original SOMDD daemon to
the ball's owned startup programs once the complete DSOM package is present.
The daemon runs with the ball's applications and stops with them. Existing
startup records and repository files are preserved. Its repository environment
inherits later application installations' SOMIR entries.

Multimedia and networking groups contain original tools and support files;
some require services 2ine does not yet support. Fonts, help books and sample
sounds/images remain in their existing separate groups. Desktop utilities,
languages/code pages, diagnostics/maintenance, DOS/Win-OS/2 files and original
OpenGL files are also available. Installing legacy files does not enable an
unsupported execution mode.

Files provided entirely by 2ine are excluded from the selectable tree. Original
alternatives such as OpenGL remain visible, with links to their graphics or
printing settings. Extracting an alternative preserves the active provider.
These optional groups start unticked unless the
current dependency check identifies a relevant missing component.

The CLI provides the same extraction in one command:

```sh
python3 -m tools.ball --ball Office.2ineball install-os2-support /path/to/OS2.iso
python3 -m tools.ball --ball Office.2ineball os2-support
python3 -m tools.ball --ball Office.2ineball install-os2-support /path/to/OS2.iso --include som
```

Missing files stay listed so you can supply more images. Nested ZIP,
PACK/PACK2, ISO, FAT, WarpIN and standard libarchive formats can be mixed
in any order without a fixed depth limit. The support files land at
`drive_c/OS2/CMD.EXE`, `drive_c/OS2/SYSLEVEL.OS2` and
`drive_c/OS2/SYSTEM/OSO001.MSG`.

For an existing installation reporting a missing system DLL such as SOMD, restore
its original dependencies from your OS/2 ISO or archive without installing the
full operating system:

```sh
python3 -m tools.ball --ball Office.2ineball audit-dependencies --source /path/to/OS2.iso
```

Repeat `--source` for a set of floppy images or additional installation media.
ISO images nested inside archives are detected automatically.
The audit follows imported DLL dependencies, including the original SOM runtime,
and copies missing files into their C: locations. Existing files and CD mappings
are preserved. The source must contain verified OS/2 installation files; a Lotus
application CD alone does not supply the IBM SOM runtime. The report identifies
remaining missing libraries and retains a log in the ball. `--no-resolve` reports
dependencies without copying files. Later audits also use remembered OS/2 media
when it is available.

Attach the original
SmartSuite ISO as D: with `map-image D /path/to/SmartSuite.iso` and copy its `ENGLISH/SUITE.RSP` into the ball's C: drive.

Edit that response file with your registration and license choices. To install
all suite components, select these seven `COMP` entries and retain the default
product installation options:

```text
COMP = Shared Components for OS/2
COMP = Freelance Graphics for OS/2
COMP = Word Pro for OS/2
COMP = 1-2-3 for OS/2
COMP = Approach for OS/2
COMP = Organizer for OS/2
COMP = DocOnline for OS/2
```

Run the original installer from a graphical session:

```sh
python3 tools/ball/engine.py --ball Office.2ineball --build build-portable --backend unicorn \
  install 'D:\ENGLISH\INSTALL.EXE' /a:I '/l1:C:\suite.log' \
  '/p:Lotus SmartSuite W4 English Standard Version' '/s:D:\ENGLISH' /x '/r:C:\suite.rsp'
```

If the optional installer-help message appears, acknowledge its dialog.
The installer writes its progress to `drive_c/suite.log` and the manager records
the process result in `logs/last-install.json`. A fresh ball launch reloads
CONFIG.SYS after the installer's reboot message. The installer configures the
five main applications, shared components and online-documentation payloads;
optional features follow the response-file selections.

The [shared C core](../ball/README.md#per-drive-and-per-application-state) stores
per-drive defaults, per-application directory/environment overrides and custom
icons. Its `browse`, `import-files`, `save-application` and `application-icon`
actions serve native frontends. Default executable icons come from their OS/2
resources. Directory and environment inheritance are evaluated by the core;
frontends do not implement competing persistence rules.

### Internal image drives

```sh
python3 -m tools.ball --ball My.2ineball device D cdrom
python3 -m tools.ball --ball My.2ineball map-image D /path/to/SmartSuite.iso
python3 -m tools.ball --ball My.2ineball create-floppy A /path/to/new.img 1440
python3 -m tools.ball --ball My.2ineball create-disk F /path/to/data.img hpfs 64
python3 -m tools.ball --ball My.2ineball media-readonly A on
python3 -m tools.ball --ball My.2ineball device-lock D on
```

ISO 9660/Joliet CDs, FAT12/FAT16 floppies, and FAT32/exFAT/HPFS disk images use
2ine's internal filesystems without host mounting. CDs are read-only. Floppies
and disks default to read/write, with selectable persistent read-only protection.
Floppy creation supports 360 KB, 720 KB, 1.2 MB, 1.44 MB and 2.88 MB.
Disk creation supports FAT32, exFAT and standard HPFS v2. Existing images and
supported primary MBR partitions can also be attached.

`device D cdrom`, `device A floppy` or `device F disk` reserves an ejected device.
`map-image` infers the type when the letter is unused. `eject D` removes the
medium while retaining D:; `map-image D /path/to/other.iso` inserts another CD,
including while the guest runs. Media must match the device type. Unmap removes
the allocation while preserving its image. Device locking prevents eject,
replacement, unmapping and changes to write protection until explicitly unlocked.
See the [shared engine's formats and limits](../ball/README.md#image-backed-drives).

In the macOS app, Install Software attaches media, suggests common INSTALL/SETUP
EXE and CMD entry points, and provides unrestricted image browsing. CMD and BAT
files use `/C CALL` through the original OS/2 command interpreter, resolved from
guest COMSPEC, mapped OS2/CMD.EXE or the original OS/2 installation CD. No host
shell interprets them. After actual process exit the manager displays the exit
status and offers Keep Attached or Eject. Locked devices cannot be ejected.
Receipts hash the selected program before launch.

Applications created by an installer through the Workplace Shell appear in the
Applications list, including documentation and license shortcuts. The default
order puts entries with extracted icons first; column sorting can select a
different order. Remove Launcher hides only that entry and keeps installed
files. Undo restores it, and refreshing registrations does not re-add a hidden
entry.

The desktop managers support separate application sessions in the same ball.
Use the Logs session selector to inspect output or stop one application.
Startup programs are shared until the last application using them exits.
Installers require the other applications in that ball to finish; other balls
remain available. Shared files, the host clipboard and the implemented OS/2
IPC APIs can connect applications, but DDE/OLE document embedding and full
SmartSuite application interoperability are not qualified.

Built-in formatting creates new images. Original OS/2 FORMAT still needs legacy
raw-device API support. HPFS DBCS, HPFS386 and active hotfix remapping are not
supported; mounting media does not boot OS/2 or qualify every installer.

## Transfer a ball as ZIP

Use Import Ball and Export Ball in the native manager, or the CLI:

```sh
python3 tools/ball/engine.py --ball Office.2ineball export office.zip
python3 tools/ball/engine.py --ball Office.2ineball export office-with-media.zip --include-images
python3 tools/ball/engine.py --ball ImportedOffice.2ineball import office.zip
python3 tools/ball/engine.py --ball ImportedOffice.2ineball map E /path/to/replacement-folder
```

Export includes installed files, settings, launchers and `2ine-export.json`
build/version information. Select **Include disk images** to bundle attached
media and image files stored inside the ball. Without that option, image files
are omitted and each media device keeps its image path as a reconnect hint: it
imports as unavailable until the image is attached again, or found and relinked
(the engine's `reference-status`, `search-references` and `relink-reference`
actions, described in [the engine README](../ball/README.md#references)). Import creates a new ball;
it never overwrites an existing ball. Paths inside the ball become relative.
Unavailable external folders retain their drive letters and appear ejected;
select the drive and use Change Mountpoint to choose a replacement folder.
Guest paths such as E:\APPS stay the same. The receiving host selects its own
2ine runtime; source build information remains in the export record.
See the [archive format and metadata](../ball/README.md#zip-import-and-export).

Graphics and media preferences live in the optional `system` fields `opengl`
(`mesa`, `original` or `off`) and `video`, `pcm`, `midi`, `dive` (booleans).
`system.window_chrome` selects `native`, `os2` or `both` title bars. The default
is `both`. Native-only mode uses the host frame for top-level application windows;
embedded OS/2 document windows retain their guest controls. OS/2-only mode hides
the host frame and uses the guest title bar and controls. Changes apply next launch.
OpenGL defaults to Mesa whenever its preference is unset. Other unset fields
inherit the receiving runtime's compiled capabilities. PCM controls
sampled/DART audio and movie sound; MIDI synthesis remains independent, and the
existing beep-volume setting still controls the PC speaker. Movies play silently
when PCM is off. DIVE is the direct video API used by applications such as Doom.

OpenGL providers are exclusive. Mesa uses the runtime's native renderer; Original
uses user-supplied OS/2 `OPENGL.DLL` and its dependencies, without falling back to
Mesa. Original rendering is not yet qualified. An unavailable explicit choice is
preserved so it can be staged before importing media or moving the ball, and
launch reports the missing capability. The `runtime-features` native action
returns requested/default/effective values and availability reasons without
modifying the ball. A runtime without Mesa reports that default as unavailable;
it does not automatically select an installed original renderer. Unrelated
programs can still launch until they request OpenGL. Original and Off require an
explicit preference. Off imports are reported as disabled and
never recovered from media; Mesa protects its supplied OpenGL module; Original
allows the original DLL to be recovered.

On macOS, launches put the ball's runtime cache first in `DYLD_LIBRARY_PATH`, then
the receiving build and inherited host entries. This keeps development-build
Mach-O dependencies and guest native API lookup in one module instance. Packaged
runtimes also carry their relocated dependency closure.


Component installation can run in the background while the component browser remains available. `os2-component-state` reads installed destinations and per-operation progress without rescanning the installation media; `cancel-os2-component-operation` requests cooperative cancellation. `remove-os2-components` removes only unchanged files recorded by recent installations. Existing files and edited files remain protected. Completed and partial operations have reports under `logs/os2-components/`.


Ordinary application launches can share a ball while retaining separate sessions, logs and Stop controls. Installation remains exclusive within that ball. Startup helpers are shared until the last manager-owned lease ends. Shared memory, queues, files and some native PM posts support cooperation; complete DDE/OLE and safe concurrent profile merging remain separate gaps.
