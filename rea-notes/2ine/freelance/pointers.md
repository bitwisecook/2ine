# Freelance cursor startup

The installed original FLG.EXE is SHA-256
`4d5a3b5ac9df801c4ec584801b0f1b313b55e27d69dd5e7c65e1c1d0cfd30cb1`.
All execution used an APFS COW copy with only its private C: mapping, IPC/profile
paths and runtime cache. The original Documents/Lotus ball was only read.
SDL video/audio were forced to dummy and rendering to software; no desktop
pointer movement or actual application UI was exercised.

The module skill and permanent API/module cache identified PMWINX.262
GETCURSORPOS and PMWINX.606 SETCURSORPOS for original provider SHA-256
`07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`.
SDK os2win.h declares `BOOL WINAPI GetCursorPos(PPOINT)` and
`BOOL WINAPI SetCursorPos(int,int)`, with WINAPI `_System` in os2wdef.h.
The original machine code establishes stack arguments and caller cleanup:

- 262 at object1+0x681f4/preferred VA0x781f4 reads one flat pointer at entry ESP+4,
  calls PMWIN.823 WinQueryPointerPos(HWND_DESKTOP,point), converts
  `point.y = screenHeight - point.y - 1`, and returns EAX1/0 with plain RET.
- 606 at object1+0x67fb0/preferred VA0x77fb0 reads x/y at entry ESP+4/+8,
  passes `screenHeight-y-1` to PMWIN.867 WinSetPointerPos, normalizes BOOL and
  returns with plain RET. Its temporary stack allocation is restored separately.
- Native call identities come from hash-matched original fixup records, not
  Ghidra's unresolved import-call displacement. The narrow decompiler's inferred
  parameters are unqualified and contradicted by the stack instructions.

The implementation uses real PM queries/setters and the shared pixel-coordinate
conversion. It retains signed coordinates outside the primary screen, checks
arithmetic overflow, writes exactly eight little-endian POINT bytes only on a
successful query, and preserves Open32 last-error on success. Context creation
uses the existing Open32 PM context. Original native error translation is not
reconstructed: context failure follows the existing allocation error8 convention,
query/set/invalid desktop extent failures use general failure31, and invalid
pointer/arithmetic uses87. No native failure is converted to success. The existing
HAL capture/released-pointer policy remains authoritative.

Standalone native tests inject PM functions and never link SDL or move a cursor.
They verify top/bottom pixel boundaries, negative/multimonitor coordinates,
extreme signed x, unaligned wire guards, Set/Get roundtrips, failure propagation
and output preservation. The LX regression forces SDL dummy, verifies checked
point spans, compares against real native PM queries/setters, and checks ESP to
prove caller cleanup4/8 with callee_pop0. Both CTest cases
`open32_pointer_coordinates` and `open32_pointers` passed after the606 rebuild;
Ruff, ty and clang-format21.1.8 passed.

Freelance then executed both cursor functions repeatedly. Dummy-driver global
warping reports its genuine unsupported failure31, which the application handles;
startup continued to the next missing import, PMWINX.306
GETMENUCHECKMARKDIMENSIONS. The bounded run ended with main exit1 after2.14s;
its private SOMDD service was terminated and joined. This is startup progress,
not a verified working Freelance application or native desktop acceptance.

`pointer-receipt.json` records source hashes and the bounded startup receipt.
The original-code evidence is retained in the permanent module cache as separate
hash-bound targeted analyses (262 and606); its `abi` lookup exposes the observed
stack/return facts without applying inferred prototypes.
