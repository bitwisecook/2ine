<!-- Derived from the original 2ine README; platform information updated in this fork. See LICENSE.txt. -->

# 2ine

*What is this?*

It's a program that loads and runs OS/2 .exe files on Linux and macOS.

*Does it work?*

It runs command line OS/2 apps, including Microsoft, IBM and Watcom developer tools,
and small Presentation Manager GUI programs such as PMStars. The native Linux
i386 and Linux Unicorn profiles also support a [basic Word Pro document editing cycle](docs/gui.md#word-pro-document-workflow)
with separately supplied SmartSuite media and settings.

*Why?*

Because.

*Does it work on Mac OS X?*

Yes. After applying copious amounts of AI to the problem, ARM64 macOS uses Unicorn
for mixed 16/32-bit x86 execution. Linux x86 can run it natively. No Linux VM or
Rosetta needed. Windows is not supported but probably wouldn't be hard for a clanker
to add.

*I have questions.*

For the original https://github.com/icculus/2ine
I have answers: icculus@icculus.org
--ryan.

For this abomination
Raise an issue on GH or find a way to contact me.
--[bitwisecook]

[Build](docs/build.md) · [Architecture](docs/architecture.md) · [Emulator](docs/emulator.md) · [Host](docs/host.md) ·
[Tools](docs/tools.md) · [Tests](docs/testing.md) · [GUI applications](docs/gui.md) ·
[OS/2 Doom](docs/doom.md) · [Sound](docs/sound.md) · [C/H formatting](docs/formatting.md) · [Licensing](docs/licensing.md)

Use a [ball](docs/ball.md) to keep application installations, drive mappings,
profiles, registry and launch settings in separate persistent environments.
The [native GNOME manager](docs/gnome-app.md), written in Rust with GTK 4 and libadwaita, provides ball creation,
configuration, installer execution and saved application launchers on Ubuntu 26.04+.
The [native macOS manager](docs/macos-app.md) uses SwiftUI and a shared C ball
engine, with a Python-free app bundle and Make targets for development and
Developer ID notarized distribution.

## Source tree

| Directory | Contents |
|---|---|
| `emulator/` | Loader, OS/2 process runtime (`lib2ine`), guest ABI, execution backends (native i386, Unicorn) and launch planning |
| `os2/` | The OS/2 DLLs, grouped like IBM's component trees (`baseos`, `ps`, `mm`, `mptn`, …); see [os2/README.md](os2/README.md) |
| `host/` | Host services: a platform-free core plus Linux, macOS, POSIX, SDL and other adapters |
| `formats/`, `storage/` | Executable, resource and audio formats; disk images, filesystems and archives |
| `ball/`, `environment/` | The ball store and its JSON facade; the OS/2 installation manager |
| `apps/` | `lx_loader`, the `2ine-ball` CLI, the GNOME and macOS managers |
| `third_party/` | Vendored libraries with their licences |
| `tools/`, `tests/`, `ports/`, `docs/` | Developer tools, tests mirroring the tree, game ports, documentation |

## Origins, credits and project sources

2ine was created by **[Ryan C. Gordon (icculus)](https://icculus.org/)**.
This fork builds on his original OS/2 compatibility layer and retains his
copyright and [zlib-style license](LICENSE.txt).

[Project sources, versions, authors and downloads](docs/project-sources.md)
lists incorporated projects, bundled assets and platform dependencies. Both
native applications expose the same catalogue in About. See also
[licensing](docs/licensing.md).

Both native clients provide [sound-bank import and MIDI channel mapping](docs/sound-banks.md).
