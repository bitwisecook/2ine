<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Flatpak

`apps/gnome/flatpak/io.github.bitwisecook.Twoine.Ball.yml` builds the GNOME manager and
the portable Unicorn runtime against the GNOME platform. Every source is pinned
by SHA-256 and the build runs offline:

| Module | Contents |
|---|---|
| `openssl` | OpenSSL 4.1.0, required by the runtime's host TLS bridge |
| `twoine-runtime` | `lx_loader`, API modules, fonts and soundfonts from the same pinned SDL 3.4.18, Unicorn (the CI revision), FFmpeg 9.0.2, FreeType 2.14.3, Liberation 2.1.5, FluidSynth 2.4.8, libInstPatch 1.1.7, liquidsfz 0.4.2 and TinySoundFont inputs as other builds |
| `twoine-gnome` | the Rust GTK 4/libadwaita manager with the statically linked ball engine, the `2ine-dialog` print and failure dialogs, desktop entry, metainfo, MIME type and icons |

The runtime is installed in `/app/lib/2ine-ball/runtime`, where the manager
finds it relative to `/app/bin/2ine-ball`.

## Cargo sources

Flatpak builds cannot download crates. `apps/gnome/flatpak/cargo-sources.json`,
beside the manifest, lists every crate in `apps/gnome/Cargo.lock` and must be regenerated whenever the lock file
changes, using `flatpak-cargo-generator.py` from
[flatpak-builder-tools](https://github.com/flatpak/flatpak-builder-tools):

```sh
python3 flatpak-cargo-generator.py apps/gnome/Cargo.lock -o apps/gnome/flatpak/cargo-sources.json
```

## Build and install

With `flatpak-builder` and the Flathub remote configured for your user:

```sh
make gnome-flatpak
flatpak build-bundle build/flatpak/repo build/flatpak/2ine.flatpak io.github.bitwisecook.Twoine.Ball
flatpak install --user build/flatpak/2ine.flatpak
```

`make gnome-flatpak` installs the GNOME SDK and the Rust SDK extension from
Flathub on first use. The crate requires Rust 1.99; use a `rust-stable`
extension that provides it.

## Permissions

The manager needs the display, GPU and audio for guest windows and sound,
network access for OS/2 TCP/IP and TLS, and access to the home folder and
removable media where balls, mapped folders and installation media live. It may
ask the file manager to show files. No other host access is requested.

Flatpak exports only icons named after the application, so `.2ineball`
packages keep the desktop's generic folder icon in a Flatpak installation.
