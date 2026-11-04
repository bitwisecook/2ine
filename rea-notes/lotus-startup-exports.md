<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Lotus forwarded exports and Open32 startup calls

The reported Freelance and Approach failures at `loader/load_lx.c:964` are
unsupported LX type-4 export bundles, not absent worker DLLs. Organizer's
`PMWINX:ordinal #224` is an absent native implementation of `GetACP`.
The dependency audit counts static file availability; it does not qualify
loader-format support or native API behavior.

## Original inputs and authority

The user-supplied installation is `/Users/jimd/Documents/Lotus.2ineball`.
All launch probes use a copied C: drive under
`build-lotus-exports-current/Probe.2ineball`, with external drives detached.

| Original input | SHA-256 |
|---|---|
| `LOTUSW4/FLG/OVRFLWKR.DLL` | `18f352c9675ada2a14337056142f37b9958a8b61c0d9b1fa6b92821b64422108` |
| `LOTUSW4/APPROACH/ovrapwkr.dll` | `a1a2626b7873ae491a9ed6e0b0c40c980129c86252a1d8acb736ebc28f87a312` |
| `extracted/install-media/os2image/FI/DAX/DLL/PMWINX.DLL` in the parent workspace | `07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655` |

Both worker DLLs mix direct type-3 exports with type-4 forwarders. Their
forwarders refer to counted strings in their own import procedure table and
to the `worker` dependency (module 1 for Freelance, module 3 for Approach).
`research/specs/lxexe.txt`, Entry Table, specifies seven bytes per forwarder,
name/ordinal target selection, chain traversal and a maximum of 1024 forwards.
The old sizing pass incorrectly advanced five bytes per forwarder as well as
rejecting the bundle in its decoding pass.

The SDK's `sdk/os2tk45-master/lib/pmwinx.lib` IMPDEF records establish
`GetACP` = 224, `GetCommandLine` = 252 and `timeGetTime` = 691.
The original PMWINX resident/nonresident names establish `GetVersion` = 1019.
Its object 1 entry offset is `0x8b21c` (analysis address `0x9b21c`);
the unpacked instructions are `B8 03 33 00 00 C3`, returning `0x3303`.
REA/Ghidra 12.2 independently decompiles this to `return 0x3303` in
`build-lotus-exports-current/getversion-rea.json`.

## Implementation and checks

The loader bounds-checks entry bundles, objects and forwarder target strings,
publishes forwarders before traversing dependencies, follows name/ordinal
chains (including circular DLL dependency graphs), rejects cyclic forwarder
chains and normalizes exports before initialization. Normalized entries retain
the target object, direct/indirect/absolute address semantics and the original
exported name/ordinal. Temporary forwarder records are freed on success and
failed loading. LX ring-2 call-gate exports remain unsupported and now have an
explicit diagnostic.

Open32 exposes `GetACP` as Windows ANSI code page 1252, consistent with its
existing `GetLocaleInfo` result and ANSI conversions. `timeGetTime` uses the
existing monotonic millisecond clock. `GetVersion` matches the original bytes.
`GetCommandLine` publishes a persistent guest buffer containing a quoted
executable and the original OS/2 command tail, with atomic first-use publication
and module cleanup.

The installed runtime reproduces all three user-reported errors; see
`build-lotus-exports-current/baseline-results.json`. The first fixed runtime
passes the forwarded exports and GetACP, reaching missing calls 691 (Freelance),
1019 (Approach) and 252 (Organizer). Those calls have also been implemented.

A complete Release build of HEAD plus these changes is retained in
`build-lotus-exports-current/runtime-make`, using the source snapshot in
`build-lotus-exports-current/source`. This avoids concurrent unrelated workspace
TLS changes that temporarily break the shared build. Configuration uses the
existing pinned FreeType/font/MIDI inputs, system SDL3/FFmpeg, embedded Unicorn,
OpenGL off and extended MIDI off; exact command output is in `configure.log`
and `build.log` alongside it.

`ctest --test-dir build-lotus-exports-current/runtime-make --output-on-failure
-R '^(lx_forwarded_exports|legacy_file_apis|open32_apis|desktop_apis|pm_dialog_apis)$'`
passes all five suites. Tests run outside the macOS sandbox: within it Unicorn
traps reading an ARM host register before guest initialization. The new LX
contract executes forwarded functions and data, mixed 16/32-bit offset bundles,
procedure queries, valid circular DLL dependencies, and malformed/cyclic
forwarder rejection. Open32 checks GetACP/GetVersion, command-line pointer
stability and argument content, and timer agreement with GetTickCount.

C/H formatting with clang-format 21.1.8, generated glue checks and focused Ruff
checks pass. No original Lotus binary has been patched.

## Final application probes and remaining limits

`python3 build-lotus-exports-current/probe.py
build-lotus-exports-current/runtime-make fixed-startup` launches the actual
installed executables in the copied ball with SDL dummy video/audio. The
unmodified Lotus binaries now pass all reported failures and the three
subsequent startup calls. Results are in
`build-lotus-exports-current/fixed-startup-results.json`:

| Application | Next observed blocker |
|---|---|
| Freelance | Missing `PMWINX:ordinal #96` (`CREATESEMAPHORE`) after loading `FLWRSEN.DLL`/`FLWRS8EN.DLL` |
| Approach | Missing `PMWINX:ordinal #729` (`REGISTERCLASSALIAS`) |
| Organizer | Guest write-protection fault at `001b:20ecc113`, memory `0x10097`, while initializing the dynamically loaded `OR1C30EN.DLL` |

The two remaining ordinal names come directly from the SHA-bound original
PMWINX name tables listed above. Organizer's fault location is runtime evidence,
not an established root cause. These are follow-up compatibility gaps, and no
full GUI startup or interactive use is claimed. The initial investigation modified no user ball and installed no app bundle.
The isolated runtime remains available for reproducing the results. Subsequent
local packaging and installation records are retained under
`build-lotus-exports-current/release/`.
