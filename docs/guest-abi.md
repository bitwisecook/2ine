<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Guest ABI

**OS/2 data stays 16/32-bit, even in a 64-bit host.**

```mermaid
flowchart LR
    Guest["Guest bytes / handles"] --> Wire["emulator/abi.h · little-endian layouts"]
    Wire --> Bind["os2/**/bindings.h · explicit pointer policies"]
    Bind --> Args["emulator/abi/arguments.c · validate, pin, copy"]
    Args --> API["Host OS/2 API implementation"]
    Guest --> IDs["emulator/abi/handles.c · typed resources"] --> Private["Private host objects"]
```

`os2/include/private/api16.json` and `api32.json` select portable imports; each DLL's
`bindings.h` holds its wire marshalling. Pointer-bearing
structures need wire marshalling. Unbound portable imports trap when called.
Pins hold mappings/resources alive; they do not freeze concurrent guest writes.

Wholly AI-authored files are marked public domain.
[Licensing](licensing.md) · [Execution diagrams](architecture.md).
