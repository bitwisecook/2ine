# Balls

A ball holds an application's OS/2 drives, configuration, installed files,
profiles, registry and saved launch commands. It uses the same loader and API
modules as an ordinary 2ine run. The CLI and GTK adapters use Python 3.11+ and the shared C ball library. The native Mac app is Python-free.

## macOS application

The native SwiftUI manager targets macOS 27+ and Swift 6.4 strict concurrency.
It builds as a self-contained `.app`, including the shared C ball engine and
complete 2ine runtime. Use `make macos-app` for Developer ID signing and notarization, or
`make macos-app-dev` for local development. See [macos-app.md](macos-app.md) for
prerequisites, credential parameters, tests and the `.2ineball` Finder format.

## GNOME application

The [native GTK 4/libadwaita app](gnome-app.md) manages balls through the shared
C backend, including drives, per-ball runtimes, saved applications, settings,
conflict-aware CONFIG.SYS drafts, undo and live activity. It follows GNOME 51
patterns and uses a ball-of-twine OS/2 icon with a simplified symbolic variant.
The GNOME guide covers source launchers, relocatable Debian/RPM packages and
the strict core26 Snap recipe.

## Command line

Build the shared engine first with `cmake -S ball -B build-ball-engine` and
`cmake --build build-ball-engine`. The CLI and GTK adapters then use that same C library.


Create a ball and map extracted installation media:

```sh
python3 tools/balls.py --ball "$PWD/balls/office" init
python3 tools/balls.py --ball "$PWD/balls/office" map D /path/to/media
python3 tools/balls.py --ball "$PWD/balls/office" --build "$PWD/build" install 'D:\ENGLISH\INSTALL.EXE'
```

The installer is an original guest executable. `install` runs it and records its
process exit status in `logs/last-install.json`. That status is independent of
whether every installed application works. Installation media and applications
are supplied separately; 2ine does not bundle them.

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

Configure settings and register an installed application:

```sh
python3 tools/balls.py --ball balls/office config system.beep_volume 0.03
python3 tools/balls.py --ball balls/office config runtime.backend unicorn
python3 tools/balls.py --ball balls/office config env.LANG en_US
python3 tools/balls.py --ball balls/office unset env.LANG
python3 tools/balls.py --ball balls/office add-app WordPro 'C:\LOTUSW4\WORDPRO\WORDPRO.EXE'
python3 tools/balls.py --ball balls/office --build build launch WordPro
python3 tools/balls.py --ball balls/office apps
python3 tools/balls.py --ball balls/office remove-app WordPro
python3 tools/balls.py --ball balls/office show
python3 tools/balls.py --ball balls/office unmap D
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
`$XDG_DATA_HOME/2ine/balls/default` (`~/.local/share` when unset). `list` lists
balls in that directory. `--build` defaults to TWOINE_BUILD or the checkout's
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
system-version files. Supply `CMD.EXE` and `SYSLEVEL.OS2` from your OS/2 system
in `drive_c/OS2`, and `OSO001.MSG` in `drive_c/OS2/SYSTEM`. Attach the original
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
python3 tools/balls.py --ball balls/office --build build-portable --backend unicorn \
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
python3 -m tools.balls --ball My.2ineball device D cdrom
python3 -m tools.balls --ball My.2ineball map-image D /path/to/SmartSuite.iso
python3 -m tools.balls --ball My.2ineball create-floppy A /path/to/new.img 1440
python3 -m tools.balls --ball My.2ineball create-disk F /path/to/data.img hpfs 64
python3 -m tools.balls --ball My.2ineball media-readonly A on
python3 -m tools.balls --ball My.2ineball device-lock D on
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

Built-in formatting creates new images. Original OS/2 FORMAT still needs legacy
raw-device API support. HPFS DBCS, HPFS386 and active hotfix remapping are not
supported; mounting media does not boot OS/2 or qualify every installer.

## Transfer a ball as ZIP

Use Import Ball and Export Ball in the native manager, or the CLI:

```sh
python3 tools/balls.py --ball balls/office export office.zip
python3 tools/balls.py --ball balls/office export office-with-media.zip --include-images
python3 tools/balls.py --ball balls/imported-office import office.zip
python3 tools/balls.py --ball balls/imported-office map E /path/to/replacement-folder
```

Export includes installed files, settings, launchers and `2ine-export.json`
build/version information. Select **Include disk images** to bundle attached
media and image files stored inside the ball. Without that option, image files
are omitted and media devices import as ejected. Import creates a new ball;
it never overwrites an existing ball. Paths inside the ball become relative.
Unavailable external folders retain their drive letters and appear ejected;
select the drive and use Change Mountpoint to choose a replacement folder.
Guest paths such as E:\APPS stay the same. The receiving host selects its own
2ine runtime; source build information remains in the export record.
See the [archive format and metadata](../ball/README.md#zip-import-and-export).
