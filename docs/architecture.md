<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# How 2ine works

**One OS/2 loader and API layer. A shared runtime over a hardware abstraction layer.**

## Structure

```mermaid
flowchart TD
    Image["NE / LX executable + DLLs"] --> Loader["loader/ · load, relocate, resolve"]
    Loader --> Guest["guest/ · wire ABI, arguments, handles"]
    Guest --> APIs["os2libs/ · OS/2 DLL implementations"]
    Loader --> Execution["runtime/execution.h"]
    APIs --> Execution
    Execution --> Native["hal/linux/i386/execution.c"]
    Execution --> Emulator["hal/emulator/execution_unicorn.c"]
    Emulator --> CPU["hal/emulator/cpu.h + cpu_unicorn.c"] --> Unicorn["Unicorn"]
    APIs --> Host["hal/include/hal/ · opaque host services"]
    Host --> HAL["hal/posix/ + platform adapters"]
    HAL --> Linux["Linux"]
    HAL --> Mac["macOS"]
    HAL -. incomplete port .-> Windows["Windows"]
```

`os2libs/` contains the OS/2 API implementations shared by both execution paths.
`runtime/` owns execution dispatch, checked guest mappings and platform-neutral
process and audio policy. `hal/include/hal/` declares host-service contracts,
and `hal/` implements them: `hal/linux/i386/` owns
Linux i386 execution and ABI bridges, `hal/emulator/` owns CPU emulation, and
`hal/posix/` shares host services between `hal/linux/` and `hal/macos/`.
`hal/win/` contains partial Windows adapters. [HAL source selection](hal.md)
chooses the implementation and dependencies for each worker. Execution adapters
know no `LxModule`, `LxMmaps` or `GLoaderState`; they do not load files or implement
DOSCALLS.
`tests/architecture/check_boundaries.py` checks that source separation.

| Boundary | Owns |
|---|---|
| Common loader | Formats, relocations, imports, modules and OS/2 identity |
| `guest/` | Fixed-width layouts, checked arguments and private resource IDs |
| `runtime/execution.h` | Entries, callbacks, threads, selectors and guest mappings |
| `hal/emulator/cpu.h` | CPU registers, contexts/x87, segments, bounded runs and faults |
| `hal/include/hal/host*.h` | VM, files, paths, libraries, clocks, threads, synchronization and child processes |
| `hal/` | Execution backends, native ABI bridges and host-service implementations |

## Source ownership

| Directory | Contents |
|---|---|
| `formats/` | Packed NE/LX definitions and decompression, without process state |
| `loader/` | Entry, imports/relocations, NE/LX loading, module lifetime and paths |
| `runtime/core/` | Bootstrap, process environment, threads, configuration and drives |
| `runtime/` | Execution dispatch/mappings and audio generator policy |
| `guest/` | Wire layouts, argument marshalling, handles and explicit binding policies |
| `os2libs/doscalls/` | File I/O, file metadata, directories, paths, memory, threads, processes, semaphores, modules, exceptions, system and audio APIs |
| `os2libs/pmwin/` | Anchor context, windows, messages and painting |
| `os2libs/generated/` | Generated export tables; public API declarations remain in `os2libs/*.h` |
| `tools/` | Generators, import analysis, assembly conversion and `lx_dump` |
| `tools/data/` | Ordinal catalogue |
| `tests/` | HAL, runtime, guest, tools, architecture and integration tests; guest programs/fixtures |
| `research/` | Format specifications and isolated experiments |

Component CMake files own their targets. Generated tables are included only by
their module's entry unit; private split-unit headers do not extend the public
OS/2 API. `mdm` and `pmmerge` are existing unbuilt placeholders, not provided DLLs.

PM child windows share their top-level host render target. Clipping honors both
class and window styles; frame statics preserve their interiors and pass pointer
hit tests through to the controls underneath. Exposure invalidates one window
tree. Message retrieval synthesizes low-priority paint requests from persistent
update regions, after posted messages and before timers, with parents painted
before children. Retrieving a paint request does not validate the update region;
BeginPaint or default paint processing does. Disabling window updates suspends
paint delivery while retaining damage; reenabling alone does not force redraw.
Presentation coalesces damaged
windows with a bounded delay. Both message retrieval paths pump host events
even when guest timers keep the queue busy.

PM clipboard metadata and system atoms are shared within the worker process.
This does not synchronize with the host clipboard. Clipboard data currently
supports handles and caller-owned pointers; transferring ownership of guest
pointer allocations and delayed rendering remain unsupported. Open32 standard
cursors use the PM system-pointer service.

## Loading

```mermaid
sequenceDiagram
    participant File as NE/LX files
    participant Loader as Common loader
    participant Exec as Execution + mappings
    participant ABI as Guest bindings
    participant API as OS/2 API modules
    File->>Loader: Read executable and DLL images
    Loader->>Exec: Allocate guest objects / segments
    Exec-->>Loader: Guest bases + separate host backing
    Loader->>Loader: Copy sections, apply relocations
    Loader->>ABI: Resolve import ordinals and names
    ABI->>API: Bind available OS/2 services
    ABI->>Exec: Emit API bridges / missing-import traps
    Loader->>Exec: Apply guest permissions, install TIB/PIB
    Loader->>Exec: DLL initialization, then program entry
```

NE USE16 segments and LX USE32 objects can coexist. Entry addresses, fixups and
wire pointers use guest addresses. Portable imports require explicit bindings;
an unbound import traps when called. Readable LX objects retain existing 386
execution compatibility, including code stored without an EXEC flag.

## Choosing the execution path

```mermaid
flowchart TD
    Start["Application build profile"] --> NativeQ{"Linux x86 + native profile?"}
    NativeQ -->|yes| Probe{"i386 worker + LDT + USE16 available?"}
    Probe -->|yes| Hardware["Native x86 execution"]
    Probe -->|no| Fail["Fail with capability diagnostic"]
    NativeQ -->|portable profile| Portable["Unicorn · mixed USE16/USE32"]
    Portable --> ARM["ARM64 macOS"]
    Portable --> IntelMac["Intel macOS · same emulated design"]
    Portable --> Other["Linux portable profile"]
    Portable -. incomplete host port .-> Win["Windows · unbuilt"]
```

Native eligibility depends on host services, not just the instruction set.
Intel macOS uses emulation too. Explicit backend requests never silently switch;
`auto` chooses among linked capabilities. Current application profiles link one
backend each. Neither Rosetta nor a Linux VM is part of macOS execution.

## macOS: translate, trap, resume

```mermaid
sequenceDiagram
    participant Loader as Common loader (ARM64 host)
    participant Exec as hal/emulator/execution_unicorn.c
    participant CPU as Unicorn x86 CPU
    participant API as Common OS/2 API
    participant Host as macOS host adapter
    Loader->>Exec: Guest entry + stack + TIB
    Exec->>CPU: Restore context, bind TLS, execute
    CPU-->>Exec: API interrupt trap
    Exec->>Exec: Save context, release engine lock
    Exec->>API: Checked / marshalled guest arguments
    API->>Host: Opaque file / thread / process operation
    Host-->>API: Normalized host result
    API-->>Exec: OS/2 return value + output bytes
    Exec->>CPU: Restore context, resume guest
    CPU-->>Exec: Return, next API, budget or fault
```

The engine schedules slices of up to 4,096 translated blocks. Checking block
boundaries avoids a callback on every guest instruction and a watchdog thread
on every guest API call. Exact instruction and time budgets remain available
through the CPU interface. Register transfers use Unicorn's batch interface;
API traps use an address hash index and reuse CPU snapshots. APIs run with the
engine stopped and unlocked. Nested callbacks preserve complete CPU contexts,
including x87 state.

## Linux x86: enter, bridge, return

```mermaid
sequenceDiagram
    participant Loader as Common loader (i386 worker)
    participant Exec as hal/linux/i386/execution.c
    participant CPU as x86 hardware
    participant Bridge as Native ABI bridge
    participant API as Common OS/2 API
    participant Host as Linux host adapter
    Loader->>Exec: Same guest entry / thread contract
    Exec->>CPU: Install LDT + FS/TIB, enter USE16/USE32
    CPU->>Bridge: Guest import call
    Bridge->>Bridge: Preserve ABI, switch private stack when needed
    Bridge->>API: Decode guest arguments
    API->>Host: Same opaque host-service contract
    Host-->>API: Normalized result
    opt OS/2 API calls back into guest
        API->>Exec: Nested callback
        Exec->>CPU: Resume suspended guest stack
        CPU-->>Exec: Callback return
        Exec-->>API: Restore host bridge frame
    end
    API-->>Bridge: OS/2 result
    Bridge-->>CPU: Restore registers / guest stack, return
```

Native execution owns descriptor syscalls, mode entry, signal handling and
private bridge stacks. Managed thread exit returns to its host entry without
unwinding C frames across machine stacks. Native API modules keep the i386 ABI,
including EAX argument and MSG hidden-word bridges.

## Windows: host port still missing

```mermaid
flowchart LR
    Loader["Common loader + guest ABI"] --> Execution["Same execution contract"]
    Execution -. port / qualify .-> Emulator["Unicorn adapter"]
    Loader --> Contracts["Opaque host contracts"]
    Contracts --> Primitives["Existing Win32 VM / sync / process primitives"]
    Contracts -. missing .-> Files["Filesystem + application / terminal integration"]
```

No Windows application build is claimed. The interfaces avoid POSIX/Windows
objects in guest layouts; the filesystem and application adapter are incomplete.

## Memory and ownership

```mermaid
flowchart LR
    Pointer["Guest linear / 16:16 pointer"] --> Check["Resolve selector, check full span + permissions"]
    Check --> Mapping["Mapping registry"]
    Mapping --> N["Native · guest address = host address"]
    Mapping --> E["Emulated · guest address maps to arbitrary host backing"]
    Mapping --> Borrow["Borrow · pin mapping lifetime"]
    Borrow --> Copy["Aligned POD / bounded string snapshot"]
    Copy --> API["Host API"] --> Back["Checked copy-back + code invalidation"]
```

Guest TIB/TIB2/PIB fields are fixed little-endian widths, never host pointers.
Exact native mappings cannot overwrite occupied worker mappings. Far spans stop
at segment limits. A borrow prevents protect/unmap until release; it does not
prevent concurrent guest writes. Pointer-bearing structures need dedicated wire
marshalling. VioGetMode/VioGetBuf remain unbound in portable USE16 execution.

```mermaid
stateDiagram-v2
    [*] --> Owned: Create typed resource
    Owned --> Pinned: Begin operation
    Pinned --> Owned: Release pin
    Owned --> Closed: Close last owner
    Pinned --> Retiring: Close last owner during operation
    Retiring --> Closed: Release last pin
    Closed --> [*]: Destroy host object
```

Guest handles map to private typed resources with generation checks. File IDs,
TIDs and legacy directory IDs have separate OS/2 numbering. Close/reuse cannot
destroy an in-flight file operation. POSIX guards release operation pins on
supported deferred cancellation.

## Threads and reentry

```mermaid
sequenceDiagram
    participant A as Guest thread A
    participant Engine as Shared engine lock
    participant API as Blocking host API
    participant B as Guest thread B
    A->>Engine: Restore A + bind A's TLS, run
    Engine-->>A: API trap, save A, unlock
    A->>API: Block
    B->>Engine: Restore B + bind B's TLS, run
    Engine-->>B: Save B, unlock
    API-->>A: Complete
    A->>Engine: Restore A + bind A's TLS, resume
```

One engine serializes guest CPU execution; host APIs can overlap. Lock order is
mapping registry before CPU engine. Whole segment allocate/free operations are
serialized so a selector cannot be reused midway through retirement. Nested
callbacks save context and restore the outer invocation on return.

Each CLI worker owns one guest address space. Process-wide adapter state does
not support several independent guests embedded in one host process. Child
programs start another worker; POSIX uses `posix_spawn`.

## Why Unicorn?

| Engine | Status / tradeoff |
|---|---|
| Unicorn 2.1.4 | Implemented. QEMU-derived embeddable CPU with explicit memory/context/trap control. |
| Direct QEMU TCG | Needs a maintained execution frontend/internal adapter. Ordinary qemu-user supplies its target OS environment; plugins instrument execution. |
| Bochs | Candidate independent interpreter/reference; needs a CPU embedding adapter. No benchmark or conformance result claimed. |
| Rosetta | Supplies neither Linux descriptor services nor a qualified mixed USE16/USE32 OS/2 path. Unused. |

NE tools and mixed runtime DLLs, including IBM CPPRMI36 and Watcom components,
need 16-bit protected-mode execution even when the final program is 32-bit.

References: [QEMU TCG](https://www.qemu.org/docs/master/devel/tcg.html),
[user mode](https://www.qemu.org/docs/master/user/main.html),
[plugins](https://www.qemu.org/docs/master/devel/tcg-plugins.html),
[Unicorn](https://github.com/unicorn-engine/unicorn/tree/2.1.4),
[Bochs](https://bochs.sourceforge.io/doc/docbook/user/features.html).

## Limits

The worker runs with host process permissions. Bounds checks and typed ownership
reduce accidental corruption; neither native nor emulated execution is a
hostile-code sandbox. Restrict the host process separately for untrusted programs.

Faults/missing imports terminate the worker with diagnostics; full OS/2 exception
delivery is unimplemented. Arbitrary concurrent module load/unload and whole-
loader asynchronous cancellation are outside the supported API scope. Exit with guest threads
leaves module teardown to OS process destruction. Retired DosFreeMem mappings
stay until worker teardown for IBM runtime compatibility. The 16-bit critical
section synchronizes participants without suspending unrelated guest threads.

See [testing](testing.md) for supported toolchain recipes. The [GUI subset](gui.md) runs selected PM/GPI applications and a bounded native
Word Pro editing workflow. Menus, dialogs, fonts and bitmap drawing are partial;
a complete desktop remains unsupported. There is no DOS
execution environment.
