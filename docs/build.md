<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Build and run

Use CMake 3.24+, a C99 compiler and Python 3.11+. Application builds also need
ncurses development files. SDL 3.4.18 is downloaded with a SHA-256 check and
built from source by default; no SDL binary is stored in this repository.

## macOS / emulated Linux

Install Unicorn 2.1.4+ and ncurses, for example `brew install cmake unicorn ncurses`
on macOS. SDL detects the optional audio and desktop development packages
available on your host. Install the packages for your intended display/audio
backend before configuring; missing optional backends are listed by SDL.

```sh
cmake -S . -B build-portable -DTWOINE_PORTABLE_LOADER=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-portable --parallel
ctest --test-dir build-portable --output-on-failure
TWOINE_BACKEND=unicorn TWOINE_LIBPATH="$PWD/build-portable" build-portable/lx_loader program.exe
```

This profile defaults on outside Linux x86. Intel macOS uses it too.
`TWOINE_PORTABLE_NE` is an older alias. An explicitly requested backend must
be linked or startup fails. Windows has no complete application adapter.

## Native Linux x86

The worker is i386, including on x86-64 Linux. Install GCC multilib, 32-bit libc
and ncurses development/runtime libraries. The kernel must support `modify_ldt`
and 16-bit descriptors (`CONFIG_X86_16BIT=y`). SDL builds in a separate i386
project. Install 32-bit development packages for any optional SDL backend you
need; the software/dummy backends support headless testing.

```sh
cmake -S . -B build-native -DTWOINE_PORTABLE_LOADER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
TWOINE_BACKEND=native TWOINE_LIBPATH="$PWD/build-native" build-native/lx_loader program.exe
```

`hal/linux/i386/dependencies.cmake` discovers matching ncurses libraries.
`-DTWOINE_BUILD_RUNTIME=ON` additionally builds the portable runtime and its
contracts with the host ABI; this does not change the i386 application worker.

## Dependency providers and offline builds

Pass `-DTWOINE_SDL3_SOURCE_DIR=/absolute/path/to/SDL3-3.4.18` to use an unpacked
source tree without a download. Its version is checked. Alternatively, install
SDL 3.4.18 and pass `-DTWOINE_SDL3_PROVIDER=system -DSDL3_DIR=/path/to/SDL3/cmake`.
The installed library must match the application worker architecture. Source
builds pin both the release and archive hash; local source/system providers
trust the supplied files. See [dependencies](dependencies.md).

`-DTWOINE_UNICORN_SOURCE_DIR=/absolute/path/to/unicorn` builds a local Unicorn
source tree. CI pins the revision recorded in [dependencies](dependencies.md).

## Runtime contracts only

SDL and curses are not required:

```sh
cmake -S . -B build-runtime -DTWOINE_RUNTIME_ONLY=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-runtime --parallel
ctest --test-dir build-runtime --output-on-failure
```

Regenerate exports with `python3 tools/lxapigen.py`. Executables (`lx_loader`,
`lx_dump`) and OS/2 shared libraries remain at the build-directory root.
Historical compiler pipelines are described in [tests](testing.md).

Application font dependencies can also be supplied offline:

```sh
cmake -S . -B build \
  -DTWOINE_FREETYPE_SOURCE_DIR=/path/to/freetype-2.14.3 \
  -DTWOINE_FONT_DATA_DIR=/path/to/liberation-fonts-ttf-2.1.5
```

The font directory must contain all twelve release TTF files and `LICENSE`.
The build copies these assets to `build/fonts/`; retain it when moving a worker.
The runtime-only profile requires neither FreeType nor these font assets.

[Optional Mesa OpenGL and macOS KosmicKrisp](opengl.md).
