<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Hardware abstraction layer

The shared runtime and OS/2 libraries call platform-neutral execution and host
contracts in `runtime/`. Implementation sources are selected by
`hal/platform.cmake`; shared source files do not select an OS with preprocessor
branches.

```text
hal/
  common/       CPU identification and argv serialization
  posix/        Shared Linux/macOS VM, files, libraries, threads and processes
  linux/        procfs queries, memory statistics and robust mutex/deadline hooks
    i386/       Direct execution, descriptors, signal handling and ABI bridges
  macos/        Darwin bootstrap queries, Mach memory queries and relative waits
  win/          Existing partial Win32 adapters
  emulator/     Host-independent Unicorn execution
```

Linux and macOS combine the shared POSIX sources with their own process, system
and synchronization adapters. Linux mutexes retain robust owner-death recovery;
macOS retains non-robust mutexes and relative condition-variable deadlines.
The shared POSIX synchronization code owns cancellation and resource accounting;
private hooks in `posix/sync_platform.h` supply the platform operations.

Direct execution is available only through `linux/i386/`. Native and emulated
execution use the same host contracts. Native i386 and portable worker targets
compile separate HAL objects, so they can be built together.

Windows has memory, clock, process, shared-memory, thread, synchronization and
library primitives extracted from the existing adapters. There is no Windows
filesystem implementation or complete application integration. CMake rejects a
full Windows build explicitly; the presence of `win/` does not imply Windows
application support.

Platform dependency discovery is colocated with its implementation:
`macos/dependencies.cmake` locates SDK curses, and
`linux/i386/dependencies.cmake` selects the existing i386 SDL/ncurses dependencies.

Add an OS-specific operation to its platform directory. Reuse POSIX code when
the semantics match on Linux and macOS; use a private platform hook when they
require different operations. CPU/compiler feature checks can remain in shared
code. The source-boundary check rejects OS-selection branches and platform-only
headers in shared HAL code.

[Architecture](architecture.md) · [Build](build.md) · [Tests](testing.md)
