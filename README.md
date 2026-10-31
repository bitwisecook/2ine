# 2ine

2ine loads OS/2 NE and LX programs and supplies host implementations of their
OS/2 APIs. This fork runs configured historical developer toolchains on ARM64
macOS through Unicorn and on x86 Linux through native execution. Both paths
use the same loader, guest-memory contracts and OS/2 API implementation.

The original project is by Ryan C. Gordon and its other contributors:
[icculus/2ine](https://github.com/icculus/2ine). This is an altered fork for the
OS/2 reconstruction project. AI tools were used to research and write its
changes. See [LICENSE.txt](LICENSE.txt) and [dependency notices](docs/dependencies.md).

## Build and run

The portable application needs a C compiler, CMake, Python 3, Unicorn 2.1.4,
SDL2 and ncurses development packages. On macOS these dependencies can be
installed with `brew install cmake unicorn sdl2 ncurses`.

```sh
cmake -S . -B build-portable -DTWOINE_PORTABLE_LOADER=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-portable --parallel
ctest --test-dir build-portable --output-on-failure
TWOINE_BACKEND=unicorn TWOINE_LIBPATH="$PWD/build-portable" build-portable/lx_loader program.exe
```

This runs directly on macOS; it does not launch a Linux VM. Rosetta is not
required. `TWOINE_PORTABLE_LOADER` defaults on outside Linux x86.
`TWOINE_PORTABLE_NE` is an alias retained for existing build configurations.

On Linux x86 the default application is a 32-bit native worker. It needs the
32-bit C development/runtime libraries, SDL2 and ncurses, and a kernel that
permits `modify_ldt` with 16-bit descriptors (`CONFIG_X86_16BIT=y`).

```sh
cmake -S . -B build-native -DTWOINE_PORTABLE_LOADER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
TWOINE_BACKEND=native TWOINE_LIBPATH="$PWD/build-native" build-native/lx_loader program.exe
```

The native configuration uses the repository's existing i386 SDL shared library
and the Debian-style i386 ncurses library path. Other Linux distributions may
need those build paths adjusted. Intel macOS uses the emulator too: x86 ISA
support alone does not supply Linux's legacy segmentation and signal services.
An explicitly requested unavailable backend fails; it never silently switches.

For just the CPU/execution/host contracts, without SDL2 or curses:

```sh
cmake -S . -B build-runtime -DTWOINE_RUNTIME_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-runtime --parallel
ctest --test-dir build-runtime --output-on-failure
```

Add `-DTWOINE_UNICORN_SOURCE_DIR=/absolute/path/to/unicorn` to build Unicorn
from source. CI uses the pinned revision in [dependency notices](docs/dependencies.md).
Regenerate checked-in API glue with `perl lxapigen.pl` after changing API
headers or `guest/bindings/*.json`.

## Compatibility and boundaries

The demonstrated gate is the six configured compiler/assembler/linker pipelines,
including running the resulting OS/2 programs. It covers Microsoft C 6.0,
MASM 5.10A and 6, DDK CL386/LINK386, IBM ICC/ILINK 3.6.5, and Watcom 11.0c.
See [reproduction and results](docs/verification.md).

This is a command-line compatibility runtime. It is not a complete OS/2 system
or a sandbox for hostile executables. Portable imports require explicit wire
bindings; unsupported imports trap when called. The configured pipelines do not
qualify all archived executables, IDEs, debuggers, GUI APIs or DOS execution.
Windows is considered in the interfaces and has some host primitives, but has
no complete filesystem/application adapter and is not a supported build.

The [architecture](docs/architecture.md) describes the execution/host split,
16/32-bit ABI, threading, lifetime rules, safety limits and emulator choice.
