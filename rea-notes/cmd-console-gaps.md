# CMD console and NLS compatibility

Acceptance: the CMD.EXE imports forwarded from DOSCALLS to KBDCALLS,
VIOCALLS and NLS resolve through portable USE16 bridges with the SDK stack
widths; text output, mode/query state and NLS buffers are functional and bounded.
This is a 2ine compatibility job, not an original kernel reconstruction gate.

## Authority and observations

The IBM Toolkit header `sdk/os2tk45-master/h/bsesub.h` supplies KBDINFO,
VIOCONFIGINFO and the Kbd/Vio signatures. The 16-bit base DDK header
`ddk/components-2004/combase/DDK/base/h/bsedos16.h` supplies DosGetCollate.
Paths are relative to the enclosing OS/2 workspace.

Original `extracted/install-media/os2image/disk_2/COUNTRY.SYS`:
SHA-256 `fcb1b9afbf3fcba8699ec7359e7f70aed84d4a2e34d7c2456eee24ed0d1a9724`.
Its directory entry for country 1/code page 437 points to 0x18ed; country 1/850
points to 0x191f. Section records identify type 6 collating weights and type 2
uppercase mappings. The selected section headers and payloads are:

| Page | Collation | Uppercase |
|---|---|---|
| 437 | `0x801d` (`FF COLLATE`, 256 byte weights at +10) | `0xb6af` (`FF UCASE  `, 128 high-byte mappings at +10) |
| 850 | `0x9e3f` | `0xc395` |

The code-page tables in nls.c express these byte-to-weight/byte-to-byte facts.
A ctypes comparison against the original file verified every one of the 256
collation weights and all 256 uppercase results for both selected pages.
No original executable body is used by these shims. Country 0 resolves to US;
page 0 resolves to the process page. Other countries and pages are rejected.

## Implemented behavior

- KbdSetCp, KbdGetCp and KbdFlushBuffer have USE16 entries. The terminal HAL
  flushes a TTY input queue and any existing curses queue without discarding
  redirected file input. KBDCALLS links VIOCALLS to share that backend.
- KbdGet/SetStatus use the SDK's 10-byte KBDINFO, preserve mode/turnaround/
  shift state and reject conflicting masks. KbdStringIn recognizes EOF.
- VioGetAnsi reports disabled translation. VioWrtTty handles carriage return,
  line feed, tab, backspace, wrap and scroll, preserving cell attributes.
  It renders through the same screen buffer as direct cell operations.
- VioSetMode supports text dimensions bounded by the host terminal and resets
  its screen/cursor. VioGetConfig reports a VGA color adapter with a bounded
  configuration prefix. VioSet/GetCp retain the selected 437/850 page.
- VioScrollUp now performs bounded rectangle scrolling, including one-cell
  rectangles and zero-count clearing. VioGet/SetCurType retain cursor state.
- Dos16GetCollate provides 256 weights, reports truncation for short buffers,
  and uses the US 437/850 mappings. Case conversion now handles their high
  bytes. Oversized country-info buffers no longer overread the host structure.
- The generator's `struct16` pointer policy first validates a two-byte cb
  prefix, then borrows/copies at most the declared size and policy maximum.
  VioGetMode/GetConfig accept short valid guest structures without requiring
  the largest structure or writing beyond the caller's bound.

## Verification

`cmake --build build-image-app-runtime -j 8` passed (log
`/tmp/2ine-console-build.log`). `ctest --test-dir build-image-app-runtime -R
'^(console_compat|host_terminal|source_boundaries)$' --output-on-failure` passed
all three checks. The new console test checks mode/config output canaries,
437/850 selection, unsupported pages, KBDINFO fields and error masks, TTY
wrap/scroll/cursor state, one-cell clearing and NLS truncation/canaries.

`python3 -m unittest tests.tools.test_tools` passed 17 tests, including the
new prefix validation/capped borrow generator check. Ruff and ty passed for
the changed generator and tooling tests; generated glue check passed.
Changed C files were formatted with clang-format 21.1.8.

The root investigation independently ran original CMD `/C ECHO CMD-WORKS`
successfully after these changes. That runtime result and its REA Evidence are
recorded with the broader CMD/installer job, not inferred from native API tests.

## Limits

Text mode only; graphical mode changes are rejected. ANSI translation is
reported disabled. VioSetCp retains the guest page, but the terminal renderer
still uses its existing PC character rendering rather than a complete 850
font implementation. Keyboard mode state is retained; the byte input adapter does not provide
a complete scan-code keyboard emulation. It now supports waiting/nonblocking
reads, a preserved peek byte and EOF without treating EOF as character 255. GetBuf's pre-existing host-pointer cast remains outside this patch;
it is not exported by a portable USE16 policy. Full interactive CMD and the
SmartSuite installation are separate runtime acceptance checks.


## REXX-dependent additions

Static fixup audit of the original classic REXX DLLs found additional console
imports: REXX.DLL KBDCALLS22 (KbdPeek16), and REXXUTIL.DLL VIOCALLS30
(VioReadCharStr16), VIOCALLS47 (VioScrollDn16), NLS8 (DosQueryCollate32).
DOSCALLS286 (DosBeep32) already existed but lacked a portable policy and bridge.
These entries are now declared, bound, and implemented. KbdPeek retains a
pending byte until consumed, so repeated peeks do not lose input. Keyboard
reads share a poll/read HAL adapter; KbdFlushBuffer also clears the pending
byte. VioReadCharStr extracts only character bytes and clips at the last
screen cell. VioScrollDn copies the rectangle bottom-to-top and fills the
new rows. DosQueryCollate32 shares the same validated country/code-page weights
and has explicit guest pointer borrows and a four-byte count output.

Artifact SHA-256 identities:

| DLL | SHA-256 |
|---|---|
| REXX | `e6bb344632ab3281f337652a9666f76ae5b69d75634b17f18eb7ba08b9c45a0b` |
| REXXAPI | `1ce3584fefb2f4c2663946777e66507a8955fc084e8bc8cf7d4603b8ae1b9cfb` |
| REXXUTIL | `4c64e5535def7da94cd36f86d577652e92b1de493ddf8fcb412e995aa6f18a89` |
| REXXINIT | `a84fb160a1cf95456ad1cfa4e0dde536b48c480e3fe6ff42af494538d216147f` |
| REXXCRT | `50461785a3929e636b937c2242b443f48fe2c3920e1646de898d1285320eaa4c` |

Audit inputs and local JSON are under `build-cmd-runtime-review/OS2/DLL/*.dump`
and `build-cmd-runtime-review/rexx-import-audit.json`. The width classification
comes from each fixup block, rather than assuming LX means all calls are
32-bit. Ordinal DOSCALLS295 is a special selector-valued data import using
16-bit offset relocations in REXX/REXXAPI, not a normal 32-bit API. The JSON's
simple width field labels it 32; this qualification takes precedence. New
DOSCALLS API gaps and that thunk data export belong to the broader runtime job.
REXXINIT/REXXCRT add no missing entries in this static audit. Static import
resolution alone does not prove every imported API's runtime semantics.

After the REXX additions, targeted build of `twoine_console_compat_tests`,
`kbdcalls`, `viocalls`, `nls` passed; log
`/tmp/2ine-console-targeted-build.log`. The three focused CTests passed again.
The expanded console test verifies repeated peek/consume, flush behavior on a
pipe, EOF, bounded KbdStringIn without consuming a byte past its buffer,
character-only reads clipped to the screen, rectangular downward scrolling,
and equality of 16/32-bit collation output with count canaries. Tooling tests
still pass 17 checks, and Ruff/ty pass. A simultaneous whole-tree build was
interrupted by an unrelated work-in-progress `test_legacy_core.c` array
assignment; the root job owns the final full rebuild and generated-glue check.

## Generated REXX API guest regression

`tests/integration/rexx_apis.py` and CTest `rexx_apis` exercise the newly added
DOSCALLS32 imports through generated guest code and the real API bridges:

- QueryProcType by ordinal/name for DOSCALLS16 and DOSCALLS32 entries, plus
  a generated mixed LX library with an explicit USE32 export and USE16 export;
  handle/name/ordinal/output errors and four-byte output canaries.
- EditName masks (`*.bak`, `???.*`), short output, invalid level/path and
  output canaries.
- Timer frequency positivity, four/eight-byte output spans, 64-bit monotonic
  ordering across DosSleep, null pointers and portable mapping boundaries.
- EnumAttribute by pathname and by HFILE pointer, two aligned DENA2 records,
  name/value-length/flag fields, first-entry index, count, empty/no-more,
  short buffers, invalid reference/level/index/pointers and output canaries.
- Shutdown reserved-argument rejection and successful guest file flushing.

The test creates isolated temporary data/xattr fixtures and synthetic binaries;
it needs no proprietary DLLs. It has backend-specific pointer-error assertions
only where the emulator can reject unmapped guest addresses safely.
The mixed DLL's entry bundles were independently decoded by `lx_dump`:
ordinal 1 is 32BIT/object 1; ordinal 2 is 16BIT/object 2. Output is in
`/tmp/2ine-rexx-mixed.dump`; generated review fixtures are under
`build-cmd-runtime-review/rexx-guest-fixtures/`.

Python compilation, fixture generation, Ruff and ty passed. The root job owns
actual guest execution and the final rebuild after concurrent API changes.
The regression exposed that native modules' unset `is_lx` flag must not make
all native APIs appear 16-bit to QueryProcType; that finding was sent to the
root implementation job before execution.

The completed full runtime build subsequently passed the generated guest
regression with:

```
TMPDIR=/private/tmp python3 tests/integration/rexx_apis.py --build build-image-app-runtime --backend unicorn
```

This run required execution outside the host sandbox: the sandboxed invocation
terminated with SIGILL before guest execution, while the approved external
invocation passed all three generated guests (`types`, `names-timers`,
`enumeration`). The by-name classification fixture now uses the existing native
export spelling `Dos16GetModHandle`; it does not require an additional historical
name alias. No production code change was needed for this fixture correction.
