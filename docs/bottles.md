# Bottles

A bottle holds an application's OS/2 drives, configuration, installed files,
profiles, registry and saved launch commands. It uses the same loader and API
modules as an ordinary 2ine run. Python 3.11 or newer is required for the manager.

Create a bottle and map extracted installation media:

```sh
python3 tools/bottles.py --bottle "$PWD/bottles/office" init
python3 tools/bottles.py --bottle "$PWD/bottles/office" map D /path/to/media
python3 tools/bottles.py --bottle "$PWD/bottles/office" --build "$PWD/build" install 'D:\ENGLISH\INSTALL.EXE'
```

The installer is an original guest executable. `install` runs it and records its
process exit status in `logs/last-install.json`. That status is independent of
whether every installed application works. Installation media and applications
are supplied separately; 2ine does not bundle them.

The bottle contains:

| Path | Purpose |
| --- | --- |
| `bottle.json` | Drive mappings, environment overrides, runtime settings and saved commands |
| `2ine.cfg` | Loader configuration generated from the bottle's mappings and settings |
| `drive_c/` | Private C: drive and installed application files |
| `drive_c/CONFIG.SYS` | Guest PATH, LIBPATH and SET variables, including changes made by installers |
| `drive_c/OS2/OS2.INI` | User profile |
| `drive_c/OS2/OS2SYS.INI` | System profile |
| `drive_c/OS2/REGISTRY.INI` | Open32 registry database |
| `drive_c/OS2/OBJECTS.INI` | Workplace Shell program and folder metadata |
| `drive_c/OS2/DLL/2INE/` | Versioned loader and native API modules used by this bottle |
| `drive_c/TEMP/` | Guest temporary files |
| `ipc/` | Bottle-scoped queues and shared-memory address assignments |
| `logs/` | Installation receipts |

Mapped directories retain their existing files. `init` preserves an existing
bottle and refuses to initialize a nonempty unrelated directory. C: belongs to
the bottle; additional drive letters can be mapped or unmapped explicitly.
Mappings are writable host directories, including media mappings. A bottle is
an application environment, with host access available through mapped paths;
it is not a security sandbox. File extended attributes use the host filesystem's
xattr support and must be preserved when copying a bottle that uses them.

Configure settings and register an installed application:

```sh
python3 tools/bottles.py --bottle bottles/office config system.beep_volume 0.03
python3 tools/bottles.py --bottle bottles/office config runtime.backend unicorn
python3 tools/bottles.py --bottle bottles/office config env.LANG en_US
python3 tools/bottles.py --bottle bottles/office add-app WordPro 'C:\LOTUSW4\WORDPRO\WORDPRO.EXE'
python3 tools/bottles.py --bottle bottles/office --build build launch WordPro
python3 tools/bottles.py --bottle bottles/office apps
python3 tools/bottles.py --bottle bottles/office show
python3 tools/bottles.py --bottle bottles/office unmap D
```

`system.trace_native`, `system.trace_events` and `system.beep_volume` are loader
settings. `runtime.backend` selects `native` or `unicorn`; `--backend` overrides
it for one command. Other application and graphics/audio environment settings
can be saved as `env.NAME`. The host's executable PATH stays intact; use OS2PATH
or the guest CONFIG.SYS PATH for guest programs. Installer changes to CONFIG.SYS
are read at each launch; explicit bottle environment settings take precedence.
The default DPATH searches C:\OS2 and C:\OS2\SYSTEM for message files.
Only SET and LIBPATH lines are interpreted. Kernel drivers and boot commands
in CONFIG.SYS are not executed by 2ine.

Workplace Shell metadata supports `WPFolder` and `WPProgram` records under the
virtual `<WP_DESKTOP>` root, stable object IDs, setup updates and deletion.
Records persist between workers and stay inside the bottle. SOM class registration,
a Workplace Shell desktop and host desktop shortcuts are not provided.

`run` and `install` accept a guest path or a host executable path inside a mapped
directory. Path lookup is case insensitive. The default working directory is
the executable's directory; `run --cwd 'C:\WORK'` overrides it. Arguments after
the executable are passed through. Subprocesses inherit the bottle's drive and
settings environment. Applications that require additional DLL directories can
add them to LIBPATH through CONFIG.SYS or `config env.LIBPATH`.

Without `--bottle`, the manager uses TWOINE_BOTTLE or the default bottle under
`$XDG_DATA_HOME/2ine/bottles/default` (`~/.local/share` when unset). `list` lists
bottles in that directory. `--build` defaults to TWOINE_BUILD or the checkout's
`build` directory. The raw loader supports an explicit TWOINE_CONFIG path; that
configuration replaces the system and user configuration files. It refuses an
unreadable explicit configuration or one with no accessible drive mappings.

Each launch copies the loader and native API modules from the selected build into a directory
identified by their content hash and execution backend. Guest module-name queries
can inspect those actual files. Existing versions remain available to running
processes. External provider assets still come from `--build`;
a bottle alone is not a standalone runtime distribution.
