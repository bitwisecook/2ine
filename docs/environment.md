<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Environment: the OS/2 installation manager

`environment/` installs user-supplied OS/2 files into a ball and keeps track of
them. It is 2ine tooling, not part of OS/2: the reimplementation of OS/2 lives
in `os2/`, and the ball itself (`ball/`) only stores settings, files and
references. Every frontend reaches it through the JSON `tb_request` facade
(`ball/facade/`); it is built as the object library `twoine_environment` and
linked into `libtwoine_ball` and `libtwoine_ball_core.a`.

It works on a ball only through the store interface `ball/store.h`, and may
use `storage/`, `formats/` and the `os2/config/` codecs. The ball store never
includes it (`tests/architecture/check_boundaries.py`).

| Directory | Owns | Actions |
|---|---|---|
| `media/support.c` | The original installer support files (CMD.EXE, SYSLEVEL.OS2, OSO001.MSG, classic REXX), remembered installation media and the media browser's JSON form | `os2-support`, `remember-os2-media`, `browse-media`, `import-os2-support` |
| `media/extract.c`, `groups.c`, `tree.c` | Category-based extraction planning and staged extraction; the component groups and the hierarchical file selection | `open-os2-source`, `plan-os2-extraction` |
| `components/store.c`, `operations.c` | The component ledger (`os2-components.json`), its ownership journal, progress, cancellation and the cooperative mutation lock | `os2-component-state`, `remove-os2-components`, `cancel-os2-component-operation` |
| `components/som.c`, `system_modules.c`, `search_paths.c` | DSOM provisioning, the modules 2ine supplies itself, and CONFIG.SYS search-list updates for installed directories | (used by the above) |
| `dependencies/audit.c`, `database.c`, `freshness.c` | Dependency audits and recovery from remembered media, the portable dependency database and its freshness tokens | `validate-os2-media`, `ensure-dependencies`, `audit-dependencies` |
| `install_records.c` | Installer receipts (`logs/last-install.json`, read directly by the GNOME frontend) | `record-install` |
| `wps_sync.c` | Launchers projected from the Workplace Shell objects in `OBJECTS.INI` | (before `command`, `save-application`, `application-icon`, `prepare-launch`) |
| `audio.c` | MIDI preferences, their validation and sound-bank import | `audio-snapshot`, `save-audio`, `import-soundfont` |

Its files keep their version 1 names inside the ball: `os2-components.json`,
`os2-media.json`, `logs/` and `cache/`. Remembered media are also recorded as
ball references (`media-1`, `media-2`, ...), so `reference-status` and
`relink-reference` cover them; relinking one rewrites `os2-media.json`.

Launch planning (`emulator/launch/`) reads the dependency database and the
support status from here; this library never calls launch planning.
