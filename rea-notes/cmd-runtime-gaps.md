# CMD.EXE and SmartSuite runtime compatibility

This is a 2ine tooling compatibility job. It does not establish a reconstructed
OS/2 core artifact or advance a kernel reconstruction milestone. The acceptance
target is original CMD execution and the original SmartSuite installation path,
with dependencies extracted from user-provided media rather than redistributed.

## Selected original artifacts

| File | SHA-256 |
|---|---|
| CMD.EXE | `98c390ba8ff4ecc9b783da92b1a5746496ca0e5e650ef78c54f24fefb5a1c308` |
| INSTALL.EXE | `fdd693a7e84cdfb2dc789ffabaa6a4034e65c09dc692e7e7a4d8754702abb586` |
| REXXINIT.DLL | `a84fb160a1cf95456ad1cfa4e0dde536b48c480e3fe6ff42af494538d216147f` |
| REXX.DLL | `e6bb344632ab3281f337652a9666f76ae5b69d75634b17f18eb7ba08b9c45a0b` |
| REXXAPI.DLL | `1ce3584fefb2f4c2663946777e66507a8955fc084e8bc8cf7d4603b8ae1b9cfb` |
| REXXUTIL.DLL | `4c64e5535def7da94cd36f86d577652e92b1de493ddf8fcb412e995aa6f18a89` |

The patched workspace REA/Ghidra 12.2 decompiled the original CMD entry point
(analysis address `0x24bdc`) under per-object protected-mode context. Evidence
`ev_67bcf0a95ca75d4450c47a2e023695a93ee4335653d9875a0fac7767ad987bd9`
is retained in ignored `build-cmd-runtime-review/build-cmd-rea-entry.json`.
The pseudocode is static evidence; API layouts follow original Toolkit/DDK
`bsedos16.h`, `bsedos.h`, `bsesub.h` and `bsexcpt.h` and verified caller bytes.

## Implemented contracts

- CMD USE16 DOSCALLS: process kill, anonymous pipes, guest date/time, shared
  segment acquisition, module loading/procedure lookup, verification state,
  directory/force deletion, explicit-length IOCTL, segment sizing, path search,
  process code page, application type, filesystem attachment and extended
  library paths. Legacy module IDs resolve back to generation-checked handles;
  output widths and far pointers remain USE16 rather than host pointer values.
- Module procedure lookup shares strict ordinal forwarding with static imports;
  an absent dynamic export returns an error instead of a callable missing-import
  trampoline. LX object bitness is retained for DosQueryProcType.
- Segment logical lengths retain separate backing capacities. Owned segments
  are released, and NE module data can shrink then grow within its reserved
  tile. Date/time adjustment is process-local and never changes the host clock.
- Queue/session, console/NLS and exception changes are detailed in
  [queue/session notes](cmd-session-gaps.md),
  [console notes](cmd-console-gaps.md), and
  [exception notes](lotus-exception-gap.md).
- Original classic REXX requires selector-valued imports and its fixed TIB
  selector, documented in [REXX selector notes](rexx-selector-compatibility.md).
  Address-based shared memory, anonymous backing and grants are documented in
  [shared-memory notes](rexx-shared-memory.md).

Original CMD page 13 offsets 3809/3882/3935 and page 16 offsets
627/683/739/923/1006/1085 contain the 998/999 far-call fixups. Their callers
push two flag words (`push 0; push 1/2/3`), proving an ULONG flags argument.
The adapters consume eight argument bytes. Flags 1/2 select begin/end paths;
3 selects LIBPATHSTRICT. Begin/end paths affect original DLL search, strict
search resolves by path, and child environments inherit the state.

## Support import

The existing recursive media importer now imports classic REXX.DLL, REXXAPI.DLL,
REXXUTIL.DLL, REXXINIT.DLL, REX.MSG, REXH.MSG, RXQUEUE.EXE and RXSUBCOM.EXE.
DLLs go to C:\OS2\DLL, messages to C:\OS2\SYSTEM, utilities to C:\OS2.
It preserves existing files and validates executable/message signatures.
Object REXX and unrelated OS/2 system files are outside this import set.
The three original baseline support files retain their completeness flag;
REXX readiness is reported separately and checked for REXX command files.

The actual Warp 4.52 installation ISO was passed to import-os2-support.
All eight REXX files were found inside os2image/disk_4/REXX (IBM PACK2),
installed into an isolated ball and reported complete. No proprietary bytes
are committed. The UI and CLI use this same extraction path.

## Validation and remaining runtime gate

- Original CMD.EXE `/C ECHO CMD-WORKS` printed the marker and exited 0.
- All 46 configured CTest checks passed on Unicorn outside the sandbox after
  the initial compatibility changes; later original-binary fixes require their
  affected checks to be rerun.
- 102 Python tooling tests passed. Swift tests passed, including native engine
  model and persistence tests. Ruff and ty passed.
- The direct original SmartSuite executable now unpacks all 25 files through
  its original NE helper (exit 0), starts its next installer process and creates
  the Lotus main frame. It remains at the optional help warning in the bounded
  headless probe; this is not a completed installation claim.
- The original REXX regression now passes SAY, CMD callbacks, original REXXUTIL
  registration, USER/private profile binary roundtrips, concurrent service reuse
  and retirement after the final client. Actual root INSTALL.CMD completes
  SysLoadFuncs/SysIni and starts the original ENGLISH/INSTALL.EXE PM loop.

The sandbox denies the host cache query used by Homebrew Unicorn 2.1.4,
causing its fallback MRS instruction to trap before any guest runs. The same
build runs successfully outside the sandbox. No dependency or host installation
was patched. Native i386 changes require Linux runtime verification; no Colima
socket was available during this work.

## No-argument macOS launch crash

The user supplied incident FEB5BD6A-F002-4B14-AA03-EEFF14D60D10 for the isolated
Installer Probe app. The stack was host strlen(NULL) in initPib, before main,
when LaunchServices supplied no guest executable. Stripping lx_loader from
host argv left argv[0] null; the existing argc guard in main ran too late.
Initialization now serializes an empty guest program safely, validates the host
argument snapshot and lets main report usage. The loader_startup regression
sets an invalid backend name to prove no CPU is needed for this path. It passes,
and the actual rebuilt probe executable also exits 1 with usage instead of
crashing. No guest code or user ball was involved in this failure.

## Tiled objects and shared pools

OBJ_TILE allocations are aligned below 512 MiB and install canonical data
selectors for every 64-KiB tile. The object extent still rounds to a 4-KiB page,
as specified by IBM's [CP Programming Reference, p.2-6](https://ftpmirror.your.org/pub/misc/bitsavers/pdf/ibm/pc/os2/OS2_2.x/IBM_OS2_2.0_Technical_Library_1992/S10G-6263-00_OS2_2.0_Control_Program_Programming_Reference_199203.pdf).
A 70,000-byte regression checks 73,728 bytes reported by QueryMem, a pointer
into the second tile, correct EAX-convention FlatToSel/SelToFlat roundtrip,
and oversized-allocation rejection. Original REXX's 32-to-16 CMD callback
requires these tiled stacks/buffers; a generic high allocation cannot be
converted by OS/2's selector arithmetic.

Suballocation has process-local attachment state. SubUnsetMem detaches only the
caller and leaves the shared free-list header intact for other processes.
Alloc/free after detachment fail until an explicit SubSetMem reattaches. Two
independent workers verify detach/reattach and continued allocation in the
other process, including preservation of an earlier live allocation.

## Final local validation

The full ARM64 application build and ball engine build pass. All 49 configured
CTest checks pass, including the original circular-DLL lifecycle expectations,
new dynamic termination callback, exceptions, selector fixups, critical-section
thread exit, independent shared-memory peers, named mutexes and no-argument
loader startup. The circular fallback keeps reverse initialization order and
retains native API dependencies until guest termination callbacks complete.

103 Python tooling tests and 21 Swift tests pass. Pinned clang-format 21.1.8,
Ruff check/format, ty and generated bindings checks pass. The explicit original
regression was rerun with the selected media in build-original-rexx-final and
passed all original CMD/REXX/service/profile cases. No proprietary inputs are
included in the commit. Native i386 runtime remains for CI; help rendering and
a completed SmartSuite installation are not asserted.

## Integration with updated main

Rebased onto the incoming sources/MIDI changes at 1b1ecad. The ARM64 runtime
build and all 49 configured CTest checks pass with TWOINE_MIDI_EXTENDED=OFF
(the production default remains ON; extra synthesis dependencies are outside
this installer validation profile). The new pinned FluidR3 bank is present.
Its stereo zone offsets apply to TinySoundFont as well as FluidSynth, and its
contrabass zones end at key 57; the MIDI regression now checks directional pan
and uses a sampled key for that instrument without changing synthesis code.

109 Python tooling tests and 23 Swift tests pass after integration. The credits
view reads the catalogue through AppModel rather than accessing its private
backend. Formatting, Ruff, ty, binding generation and project-source checks
pass. The supplied original CMD/REXX regression passes again in
build-original-rexx-integrated, including original REXXUTIL/profile operations
and the separate shared service's last-client retirement. The no-argument
loader startup regression also passes. Extended MIDI and native i386 are not
claimed as locally tested profiles.
