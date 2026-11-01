# Separately rebuilt Doom source variant

The unchanged archived OS/2 Doom executable remains preserved. Its published
I_GetTime source uses only DATETIME.seconds and hundredths, causing time to go
backwards at each minute. No workaround changes DosGetDateTime semantics.
The user-authorized fully working application profile therefore uses an
explicitly identified source build with a monotonic game clock.

Source input is the published os2doom110-src.zip from os2site.com, SHA-256
b4de031a2316d529f504d21f2095768aafa88eb789a7c46415f43186e53d15b9. This is the
separately archived source release, not a byte-identity reconstruction or a
claim that every source unit matches the shipped executable. The original
makefile os2doom.mk1 names 68 C units and tmap.asm. Its compiler flags are
preserved by tools/src/2ine-hal/tools/build_doom.py, using retained Linux
Open Watcom 1.9 wcc386/wasm/wlink, OS/2 SDK4.5 import libraries and matching
Watcom runtime. Program remains OS/2 LX, executed under 2ine.

Functional patch:

- I_GetTime queries QSV_MS_COUNT, accumulates unsigned millisecond differences
  in a 64-bit total and computes game tics. DosEnterCritSec/DosExitCritSec
  serialize the query and update across UI RenderScene and game thread calls.
  A failed query preserves the previous game time.
- Published source already sets fullscreen default0; no new default change.
- Network startup calls G_InitNew directly after arbitration. The former
  G_DeferedInitNew path called G_DoNewGame, clearing netgame, playeringame,
  consoleplayer and command-line game settings before the first level tick.
- The port's ticcmd_t has four int fields and two trailing padding bytes.
  PacketSend/PacketGet now copy the complete record and convert angleturn and
  consistancy with htonl/ntohl. The former 16-bit conversions and uninitialized
  padding caused valid peer packets to fail NetbufferChecksum. Record layout
  and checksum algorithm remain unchanged. See network-game.md for real peer
  evidence; these are explicit application source fixes, not OS/2 API changes.

Necessary build corrections are retained in research/patches/doom110-clock.patch:

- qmus2mid.c's division parameter now agrees with its existing int2 declaration.
- i_net.c explicitly selects TCPV40HDRS and utils.h, retaining the old stack's
  16-bit sockaddr family and correct _System byte-swap/address declarations.
- tmap.asm uses .MODEL flat with its already decorated external symbol names.
  External data are DWORD rather than NEAR. The first source runtime exposed
  the distinction: ESI=0x121b8c/EBP=0x121bb4 matched the addresses of _dc_source
  and _dc_colormap, while the stride on the stack was 0x68a88, the address of
  _SCREENWIDTH. Watcom1.9 had emitted immediate symbol addresses instead of
  dereferencing these incorrectly typed data externs. Explicit DWORD types
  retain the published assembly algorithm and make real rendering work.
- Builder lowercases the local archive filenames for case-sensitive includes,
  removes DOS EOF bytes and translates backslashes only in scratch SDK include
  operands. Retained SDK/third-party source inputs are unchanged.
- Linker explicitly binds Gre32Entry3/5 to authentic PMGRE ordinals63/65;
  OS2386, MMPM2, SO32DLL, TCP32DLL and matching C-runtime inputs are declared.

Build command from workspace:

```
python3 tools/src/2ine-hal/tools/build_doom.py --source-zip .state/2ine/doom/downloads/os2doom110-src.zip --sdk sdk/os2tk45-master --watcom compilers/open-watcom-1.9/c-cpp --output .state/2ine/doom/repro-source110
```

The builder writes per-unit logs, exact link recipe, patched-source/header/tool
and library hashes to build.json. Archive compiler files lack execute bits;
the builder copies tools into its output directory before setting those copies
executable. The native tools need approved execution outside the sandbox.

Clock contract passes with the actual shipped patch body: the minute boundary,
uint32 millisecond rollover, API failure/resume, and 8 concurrent callers each
making10000 updates. Python test: tests/tools/test_doom_build.py. Ruff0.15.20
and ty0.0.57 pass for builder/test.

An isolated initial full verifier confirms actual rendering, movement/mouse/
fire and first save from the renderer-corrected build. A second-slot save used
old worker arrow-scan translation and failed; PM input mapping is being corrected
by the primary agent. This partial run is not a full gameplay pass or DONE.
Final profile/70-second evidence is owned by the primary game verifier receipts.

Final clock-state serialization is frozen in the patch. Native 70-second source
runtime succeeded in the primary agent's exploratory verifier after corrected
PM arrow-scan mapping. The final acceptance verifier remains separate.

A second clean build initially differed in only the original __TIME__ string.
The builder now replaces __DATE__/__TIME__ occurrences in its scratch i_main.c
with explicit --build-epoch metadata (SOURCE_DATE_EPOCH, default0). This is
synthetic source-build metadata, not a recovered original date. The input
receipt records it. Two independent output directories now reproduce complete
executable bytes with identical declared inputs and epoch.

After the network fixes, two clean complete builds in network-source-final
and network-source-repro reproduce SHA-256
a31b8d68c6d7b7d8fbff6ccb007f96b3bfd3ece82f926ae8d5e0cfe6038be9cd.
Their build.json receipts retain every input and patched-source identity.


Final accepted source variant is built independently in save-source-final and
save-source-repro, byte-identical SHA-256
`7604e9aa38e91c862ea07cd25ec4aa7578a912b844c75ad7d62cb9907b532aee`.
It includes the network fixes, synchronized stop-before-free audio shutdown
and dedicated save archive storage. Both full game/network gates pass with
this exact executable; [completion evidence](finish.md) records scope and
receipts. The three actual patched-function contracts test clock, shutdown
and large-save storage. No original archived executable is changed.
