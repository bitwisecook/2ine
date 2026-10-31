<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Shared runtime

**One execution boundary. Separate host services.** Neither backend owns the
loader or OS/2 behavior.

```mermaid
flowchart TD
    Loader["Common loader + OS/2 APIs"] --> Exec["execution.h"]
    Exec --> Memory["execution_memory.c · checked guest mappings"]
    Exec --> Native["native/execution_linux_i386.c"]
    Exec --> Emu["emulator/execution_unicorn.c"]
    Loader --> Host["host*.h · opaque host objects"]
    Host --> Platforms["Linux / macOS adapters"]
```

`execution.h` covers program/DLL/thread entry, callbacks, bridges and selectors.
`host*.h` covers files, libraries, threads, processes, synchronization and VM.
Guest addresses, host pointers and host handles are distinct.

[Full diagrams](architecture.md) · [Guest ABI](guest-abi.md) ·
[Licensing](licensing.md).
