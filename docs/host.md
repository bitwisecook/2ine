<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Host services

The shared runtime and OS/2 libraries call platform-neutral execution and host
contracts in `host/include/host/` and `runtime/execution.h`. A host function
has a common core and one adapter per platform or library, and every adapter
directory uses the core's function names:

```text
host/
  include/host/          Public contracts, included as "host/<name>.h"
  core/<function>/       Platform-free shared code and the hook contracts adapters implement
  <adapter>/<function>/  An adapter's part of that function
  <adapter>/sources.cmake  The adapter's files, by function
  platform.cmake         The adapters chosen for each function on this host
  CMakeLists.txt         Links groups of functions into workers and modules
```

| Adapter | Implements |
|---|---|
| `posix/` | VM, clocks, processes, threads, IPC, files, sockets, fatal reports, desktop registry and CUPS printing shared by Linux and macOS |
| `linux/`, `macos/` | procfs/Mach queries, robust or relative waits, file ranges and metadata, ICMP, routes and DNS, report hooks, dialogs, GL driver policy |
| `win/` | Partial Win32 memory, clock, process, shared-memory, thread, synchronization, library, clipboard and print adapters |
| `sdl/` | SDL3 windows, rendering, input, audio, clipboard and the fallback fatal dialog |
| `openssl/` | TLS transport |
| `freetype/` | Font discovery and rasterisation |
| `curses/` | Character-cell terminal |
| `mesa/` | EGL rendering contexts for OpenGL |
| `none/` | Stubs for functions a port does not integrate natively |

Functions: `system` (memory, clocks, OS and CPU names), `process`, `thread`
(threads, thread-local values, synchronization), `ipc` (shared memory,
mailboxes), `fs`, `net` (sockets, resolver, interfaces, raw ICMP/UDP, TLS),
`fatal` (report and dialog), `window`, `input`, `desktop`, `audio`,
`clipboard`, `print`, `terminal`, `font` and `opengl`. A missing adapter
directory is a missing port.

The core is the C form of a base class: the contract header plus shared code
that calls a small set of adapter hooks. Exactly one adapter supplies each hook
in a build, chosen at link time by `platform.cmake`. Hook contracts that only
POSIX adapters share, because they use POSIX types, live in `posix/` beside the
shared code that calls them: `posix/thread/sync_hooks.h` (robust mutexes and
condition clocks), `posix/fatal/report_hooks.h` (registers, loaded images and
dialog spawn) and `posix/net/interfaces_hooks.h` (MAC addresses, routes, DNS).

Linux mutexes retain robust owner-death recovery; macOS retains non-robust
mutexes and relative condition-variable deadlines. The shared POSIX
synchronization code owns cancellation and resource accounting.

Execution backends are not host services: `emulator/execution/native_i386/`
runs guest code directly on Linux x86 and `emulator/execution/unicorn/`
emulates it elsewhere. Both use the same host contracts. Native i386 and
portable worker targets compile separate host objects
(`twoine_add_host_services`), so they can be built together.

Windows has memory, clock, process, shared-memory, thread, synchronization and
library primitives. There is no Windows filesystem, network or fatal-report
adapter and no complete application integration. CMake rejects a Windows build
explicitly; the presence of `win/` does not imply Windows application support.

Dependency discovery: `macos/dependencies.cmake` locates SDK curses,
`emulator/execution/native_i386/dependencies.cmake` selects i386 ncurses and
FreeType, and `cmake/SDL3.cmake` selects the SDL provider.

Add an OS-specific operation to its platform adapter. Reuse POSIX code when the
semantics match on Linux and macOS; use a hook when they require different
operations. CPU/compiler feature checks can remain in shared code. The
source-boundary check rejects OS-selection branches outside `linux/`, `macos/`
and `win/`, host APIs in `core/` and `include/`, adapter includes of other
adapters (except Linux and macOS of `posix/`) and private host headers outside
`host/`.

[Architecture](architecture.md) · [Build](build.md) · [Tests](testing.md)

Audio generator policy lives in `runtime/audio.c`; SDL owns the stream and device
conversion. A stream stays open until runtime shutdown. Closing PM video does
not close audio. PM windows/messages/painting use opaque host windows and
normalized rectangles/events. VIO retains its OS/2 screen buffer and delegates
terminal rendering to `curses/terminal/terminal.c`. TCP/IP translates OS/2
socket structures and flags before calling `posix/net/socket.c`. Host network
identity queries use `posix/net/interfaces.c`; modern TLS uses OpenSSL behind
`host/tls.h`. See [host networking and TLS](networking.md) for policy and ABI
coverage. No SDL or curses types cross the public host boundary.

[Digital-video frames](video.md) cross `host/video_frame.h` as borrowed planar
YUV420/NV12 or BGRA with explicit stride, length and colorspace. Decoder-owned
frames reach the PM owner through a retained mailbox. SDL reuses one streaming
texture and uploads each frame once before drawing its visible rectangles.
Workers use the shared audio mixer and never call the renderer.
