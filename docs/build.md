<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Build and run

## macOS / emulated Linux

Install a C compiler, CMake, Python 3, Unicorn 2.1.4+, SDL2 and ncurses development
packages. macOS: `brew install cmake unicorn sdl2 ncurses`.

```sh
cmake -S . -B build-portable -DTWOINE_PORTABLE_LOADER=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-portable --parallel
ctest --test-dir build-portable --output-on-failure
TWOINE_BACKEND=unicorn TWOINE_LIBPATH="$PWD/build-portable" build-portable/lx_loader program.exe
```

The portable profile defaults on outside Linux x86. Intel macOS uses it too;
Rosetta is not required. `TWOINE_PORTABLE_NE` is an older alias for the option.
An explicitly requested backend must be linked or startup fails.

## Native Linux x86

The worker is i386, even on x86-64 Linux. Install 32-bit C development/runtime
libraries, SDL2 headers/runtime and ncurses. The kernel must support `modify_ldt`
and 16-bit descriptors (`CONFIG_X86_16BIT=y`).

```sh
cmake -S . -B build-native -DTWOINE_PORTABLE_LOADER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
TWOINE_BACKEND=native TWOINE_LIBPATH="$PWD/build-native" build-native/lx_loader program.exe
```

This profile uses the existing repository i386 SDL shared library and a
Debian-style i386 ncurses path. Other distributions may need path adjustments.
On Ubuntu CI, host SDL headers are combined with the i386 configuration header
extracted from its development package; see `.github/workflows/main.yml`.

## Runtime contracts only

No SDL or curses needed:

```sh
cmake -S . -B build-runtime -DTWOINE_RUNTIME_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-runtime --parallel
ctest --test-dir build-runtime --output-on-failure
```

`-DTWOINE_UNICORN_SOURCE_DIR=/absolute/path/to/unicorn` builds a local Unicorn
source tree. CI pins the revision in [dependencies](dependencies.md).

Regenerate API glue: `python3 lxapigen.py`.
Run historical tool pipelines: [tests](testing.md).
Windows has partial primitives but no complete filesystem/application adapter.
