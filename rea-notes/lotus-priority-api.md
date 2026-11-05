# Approach: PMWINX ordinal 636

The installed Approach import table contains two fixup records for PMWINX
ordinal 636, `SETPRIORITYCLASS`. The SDK declares `BOOL WINAPI
SetPriorityClass(HANDLE, DWORD)` in `sdk/os2tk45-master/h/os2win.h:7026`;
OS/2's WINAPI macro selects `_System`, not Win32 stdcall.

Original PMWINX SHA-256
`07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`
was inspected with patched REA/Ghidra 12.2. Evidence
`ev_1c44cd46af2729f9e7b241001f6a213c5baa87fd6e3c7bee57ab4958cbaac81e`
records entry `0x976f8` through `0x97884`, two stack arguments at EBP+8/+12,
and plain RET exits: eight argument bytes, caller cleanup. The complete
disassembly and decompiler output are retained under
`build-lotus-priority/setpriorityclass-rea.json`. Pseudocode is analyst
evidence, not reconstructed source; the original import call is not faithfully
rendered by its default prototype.

The selected bytes validate class values `0x20`, `0x40`, `0x80` and `0x100`;
other values return zero through the error helper with `0x133`. An invalid
process handle returns zero through the error helper with 6. The decoded
mapping passes OS/2 process priority classes 2, 1, 2 and 3 respectively, delta
zero and the process record's PID to the imported priority operation. Success
stores the requested public class in the process record and returns one.
These are observations of this original build, not a host-scheduling design.

2ine does not currently export ordinal 636. The dependency-file index correctly
reports the DLL graph available, but that does not establish export or ABI
coverage. The complete Lotus ordinal census records this gap alongside the
other imported APIs; a successful placeholder is not an implementation of
process scheduling or priority queries.
