# Shared C ball engine

This library owns the cross-platform ball format and behavior. macOS SwiftUI
calls its C ABI directly. The CLI and GTK adapters use the same library via
ctypes. No Python interpreter or script is shipped in the macOS application.

Build independently of the OS/2 runtime and UI dependencies:

```sh
cmake -S ball -B build-ball-engine
cmake --build build-ball-engine --config Release
```

Outputs: `twoine_ball` shared library, `libtwoine_ball_core.a` static archive
(`twoine_ball_core.lib` on MSVC), and the `2ine-ball` native JSON helper.
The public header is `include/twoine_ball.h`. ABI version 1 has no C structs
crossing language boundaries. Inputs/results are UTF-8 JSON; errors and results
are owned allocations that the caller frees with `tb_free`. `tb_request` is
synchronous and reentrant. Run it on a worker actor/thread, serialize changes
to the same ball, and never use it from the UI event loop for hashing/copying.

Request actions:

| Action | Input | Result |
| --- | --- | --- |
| `discover` | none | Default ball directories |
| `snapshot` | `ball` | `manifest`, canonical `roots`, effective `workingDirectories`, guest `environmentDefaults`, opaque `applicationRecords`, `unavailableDrives` path hints, Latin-1 `configSys` text |
| `command` | `ball`, `arguments`, optional `build` | Updated snapshot |
| `runtime-build` | `ball`, optional `build` | Snapshot with selected runtime; omitting `build` removes the external runtime selection and preserves the backend |
| `save-config-sys` | `ball`, `expected`, `text` | Snapshot; rejects a conflicting disk edit |
| `prepare-launch` | `ball`, `build`, `arguments` | Structured command, cwd, environment changes, receipt seed |
| `record-install` | `ball`, `receipt`, `programHost`, `exitCode` | Snapshot after recording the actual result |
| `host-path` / `guest-path` | `ball`, `path` | Canonical host path / mapped guest path |
| `browse` | `ball`, `path` | Drive-letter directory, canonical parent and entries (`name`, guest `path`, `hostPath`, `directory`) |
| `import-files` | `ball`, destination guest `path`, host `sources` array | Updated directory after a non-replacing recursive copy |
| `set-working-directory` | `ball`, absolute guest `path` | Snapshot with that drive's saved default |
| `save-application` | `ball`, `name`, `program`, `arguments`, optional `originalName`, `workingDirectories`, `environment`, `iconSource` | Snapshot after one validated manifest write |
| `guest-environment` | `ball`, optional app `environment` | Expanded effective guest variables |
| `application-icon` | `ball`, app `name` or guest `program` | `source` (`embedded`, `custom`, `none`), optional base64 `rgba` with dimensions or native image bytes in `image` |
| `image-preview` | Host image `path` | Native image data for frontend preview |
| `parse-arguments` / `format-arguments` | `text` / `arguments` | Structured argv / losslessly quoted display text |

Commands are arrays such as `["init"]`, `["map", "D", "/media"]`,
`["config", "env.LANG", "en_US"]`, `["unset", "system.trace_events"]`,
`["add-app", "--cwd", "C:\\TEMP", "App", "C:\\OS2\\APP.EXE", "a b", ""]`,
and `["remove-app", "App"]`. Launch arrays begin with `run`, `install` or `launch`.
The frontend owns asynchronous process monitoring, native logs/windows and UI
lifecycle. The C engine returns `unsetEnvironment` and `environment`; apply both
to the inherited host environment before spawning. Send the actual exit code
to `record-install`; negative values represent POSIX signals. No shell is used.
For Windows file pickers, `guest-path` explicitly interprets its input as a host
path, avoiding ambiguity between a host C: path and the guest C: drive.

The helper reads one request from stdin and prints its JSON response. With
`--execute`, a `prepare-launch` request instead starts the guest with the plan,
streams its output and returns its exit status, recording an installer receipt
when appropriate. POSIX uses fork/exec/wait; Win32 uses Unicode CreateProcess
with explicit CRT-compatible argument quoting. Native frontends can handle
process presentation themselves while reusing the C launch plan.

`platform.c` provides POSIX and Win32 file adapters. Windows paths use wide
filesystem APIs. Library/CLI builds are covered by the Linux/macOS/Windows CI
matrix; the full OS/2 runtime's supported hosts remain separately documented.

Vendored dependencies:

- cJSON v1.7.19, commit `c859b25da02955fef659d658b8f324b5cde87be3`, MIT.
  Local changes make its error storage thread-local and retain parsed number
  lexemes so unrelated 64-bit manifest metadata survives edits exactly.
- Brad Conte's public-domain SHA-256 implementation, commit
  `cfbde48414baacf51fc7c74f275190881f037d32`. Used for runtime identity and
  installer receipts, verified against Python hashlib in the engine tests.
- FatFs R0.16 with upstream patches 1 and 2, July 10, 2026, under its permissive
  license. Used for FAT32/exFAT, with in-process image-file disk callbacks. See
  [configuration and local changes](vendor/fatfs/README.md).

## Per-drive and per-application state

The optional ball `workingDirectories` object maps letters to absolute OS/2
paths, for example `{"C":"C:\\DOOM","D":"D:\\INSTALL"}`. Unset entries default
to their drive root. `currentDrive` is used for unqualified paths. `D:SETUP.EXE`
uses D's own default; `D:\\SETUP.EXE` starts at D's root. Canonical paths cannot
escape the drive they name, including nested or duplicate mappings.

Saved applications may carry `workingDirectories` and `environment` overrides.
The program's drive selects the initial active directory; explicit legacy
`cwd`/`--cwd` still selects the process working directory. Every other drive
retains its own inherited or overridden directory. The core emits
`TWOINE_DRIVE_CWD_A` through `TWOINE_DRIVE_CWD_Z` for mapped drives and
`TWOINE_CURRENT_DRIVE`; the runtime seeds its per-drive state and passes current
values to guest subprocesses. Launch preparation never changes ball defaults.

Guest environment precedence is built-in defaults, CONFIG.SYS, ball overrides,
then application overrides. In overrides `%NAME%` is case insensitive and reads
the inherited layer or another override. A self-reference uses the inherited
value, so `PATH=%PATH%;C:\\DOOM` appends a directory. Cycles are rejected; undefined
references expand to an empty string. OS/2 path lists use semicolons. Guest PATH
is exported as OS2PATH, preserving the host PATH. Reserved host/control
variables cannot be overridden. Override evaluation is independent of JSON key
order. Empty strings are explicit values; absent entries inherit.

`save-application` validates all fields before changing the manifest and retains
unknown application metadata. `iconSource` imports a host image into a
content-addressed `icons/` asset; an empty string restores automatic extraction.
Omitting the field retains the existing choice. Default icons are decoded in C
from LX or OS/2 NE RT_POINTER resources, including bitmap arrays, monochrome
and color AND/XOR images, and iterated/EXEPACK2 LX pages. The desktop and PM use
the same bitmap decoder. Unsupported/missing embedded artwork returns `none`.
The frontend creates its native image object from the returned bytes. No guest
code runs to extract icons. Image assets stay in the ball when a launcher is
removed. The snapshot's opaque `applicationRecords` and `restore-app` command
allow Undo without frontend reserialization or loss of unknown fields.

Imports preflight the complete batch, reject conflicts and symbolic links,
and copy without deleting sources or replacing existing destinations. An I/O
failure may leave already copied items; the diagnostic states that explicitly.
Frontends own native drag/drop, pickers, image presentation, OS configuration
and asynchronous operation presentation. The core owns these data/path rules.

Format references: [IBM LX format](https://komh.github.io/os2books/os2tk45/lxref.htm),
[Open Watcom NE structures](https://github.com/open-watcom/open-watcom-v2/blob/master/bld/watcom/h/exeos2.h).

## Image-backed drives

`["map-image", "D", "/media/SmartSuite.iso"]` attaches an image directly through
2ine's internal filesystem. A drive value may be a directory string or a device
descriptor such as `{"device":"cdrom","image":"/media/SmartSuite.iso"}`.
`{"device":"floppy"}` reserves an ejected drive. `device LETTER cdrom|floppy|disk`
reserves an unused letter; `eject LETTER` removes its medium while retaining the
allocation. `map-image` inserts or replaces matching media. `unmap` removes the
allocation. C: remains private to the ball and cannot be image-backed. Older
image-only descriptors remain readable.

ISO 9660/Joliet CDs are read-only. Raw FAT12/FAT16 floppy images default to
read/write. FAT32, exFAT and standard HPFS v2 disk images support existing images
and creation, read/write access, directories, long filenames and file attributes.
Supported primary MBR partitions are handled internally. No host disk attachment,
mount point or extracted drive tree is created. HPFS also preserves and exposes
native OS/2 extended attributes; FAT and exFAT cannot store OS/2 extended attributes.
Filesystem queries expose the volume type, label, geometry, capacity and free space.

`create-floppy LETTER /path/new.img KIB` creates and attaches a formatted FAT12
image. Supported capacities are 360, 720, 1200, 1440 and 2880 KiB.
`create-disk LETTER /path/new.img fat32|exfat|hpfs MIB` creates and attaches a
formatted disk image. FAT32 requires at least 64 MiB; exFAT and HPFS accept at
least 8 MiB. The current HPFS formatter supports up to 64 GiB, in 8 MiB increments.
Creation never overwrites an existing file. `media-readonly LETTER on|off` controls
floppy/disk write protection, including already open writers. The chosen mode
persists when changing images. CDs remain read-only. `device-lock LETTER on|off`
locks an attached image and its access mode: replacing, ejecting, unmapping and
changing protection are rejected until the device is explicitly unlocked in ball
settings. Descriptors retain the optional `readonly` and `locked` booleans.

Internal member paths are opaque and must be passed to the engine/HAL rather
than opened as host paths. A stable per-ball device path reads the atomically
replaced `devices/LETTER.media` descriptor on each operation, so a running guest
sees media swaps. Ejected access returns OS/2 ERROR_NOT_READY. Handles retain a
private member snapshot after eject/replacement; writes require the same source
to remain attached and writable. Source-image locks serialize access across
processes. Writes update the image itself; make a separate copy when the original
must be preserved. These filesystems do not provide a transactional image backup.
Images remain referenced by filename. Missing sources become unavailable without
preventing the rest of the ball from opening.

The native executable picker browses ordinary guest paths such as
`D:\ENGLISH\INSTALL.EXE`. Installer launch defaults to the selected program's
parent directory; explicit overrides take precedence. CMD/BAT selection resolves
the original OS/2 command processor through guest COMSPEC, then mapped OS2/CMD.EXE,
CMD.EXE or the OS/2 CD's OS2IMAGE/DISK_2/CMD.EXE. It runs `/C CALL` through that
interpreter; missing interpreters produce an actionable error. No host shell
interprets guest scripts. The host process starts in the ball's C: directory while
the guest retains the image's per-drive working directory. The receipt captures
the selected program hash before execution, so later ejection cannot change it.
Snapshots expose `driveRecords` for opaque Undo/Redo and `imageSources` for the
resolved source filenames, including ball-relative media paths.

Limits: HPFS uses supported single-byte code pages; DBCS, HPFS386 and active hotfix
remapping are rejected. Flux floppy formats, raw 2352-byte CD tracks and
multi-extent ISO files are unsupported. These are filesystem devices, not a
bootable OS/2 VM or raw-sector/DASD interface. Original OS/2 FORMAT.COM/FORMAT.EXE
still requires missing legacy disk IOCTL/export support; use the built-in image
creation controls for formatting. Attaching original media does not establish
that its full installer or installed applications are supported. Qualification
compares actual OS/2 and SmartSuite CD listings and selected bytes against
libarchive, and validates writable images with independent filesystem checkers.
Original installation media is supplied separately and is never committed.

## ZIP import and export

`export-ball` takes `ball`, output ZIP `path`, optional `includeImages` (false
by default), and optional selected runtime `build` and frontend `exporter` object.
It returns the ZIP path. `import-ball` takes a ZIP `path` and a **new** destination
`ball`; it returns the imported snapshot. Both actions use the same C API as the
native managers. The CLI exposes `export archive.zip [--include-images]` and
`import archive.zip` with the destination selected by `--ball`.

Archives contain `ball.json` and the ball's private files at their root.
Bundled attached images live under `bundled-media/` and use ball-relative
manifest paths; unattached image files already inside the ball are included
when requested. Without images, mapped media devices remain allocated but
are ejected, and common image extensions inside the ball are omitted. External
mapped folders retain their previous paths as reconnect hints. Missing or
foreign-host paths keep their drive letters but are ejected until reconnected.
Folder paths inside the ball are normalized to relative paths with forward
slashes. Imported balls select the receiving host’s runtime: external build
locations and cached native runtime binaries are omitted from the portable
configuration, while the original runtime identity stays in 2ine-export.json.

`2ine-export.json` records the export time, image option, engine ABI, Git
version/revision (including the dirty marker), compiler/build time, frontend
version/build when supplied, and selected runtime settings. An available runtime
also records its content identity and loader SHA-256. New CMake builds emit
`2ine-build.json` with source version/revision, build configuration, platform,
architecture and compiler; exports retain that record as runtime build
information, and macOS bundles copy it with the runtime. Older/unavailable
runtimes are identified by their fingerprints or explicitly marked unavailable;
missing source metadata is never inferred from the exporter's source revision.
Imports preserve the export record; a later export creates a fresh record.

Generated media controls, IPC and `2ine.cfg` are recreated for the destination.
Snapshots expose `unavailableDrives` as drive letters and previous path hints;
these drives stay in the manifest, preserve guest working directories and
launchers, and do not prevent other drives from working. Choose Change Mountpoint
in Drives (or `map LETTER folder` in the CLI) to reconnect a folder. Missing
image paths behave as ejected media and use the existing image attach controls.
A folder reconnects on refresh if its original path becomes available again.
The private C: folder remains required.
Imports reject links, special/encrypted entries, traversal and duplicate files,
and remove a failed import. Existing destinations are never replaced. Exports
reject symbolic links and destinations inside the source ball, stream file data,
and replace the destination ZIP only after successfully completing the archive.
Stop guests before archiving; the managers serialize these operations.

ZIP support links against libarchive (system library on macOS, `libarchive-dev`
on Debian/Ubuntu, `libarchive` via vcpkg on Windows). The BSD-licensed API headers
and license in `vendor/libarchive/` are pinned to
[libarchive v3.7.4](https://github.com/libarchive/libarchive/tree/v3.7.4), allowing
macOS SDKs that omit the headers to use the system library.
