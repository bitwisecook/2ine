# Approach: counted Open32 Unicode conversions

Original provider: `PMWINX.DLL`, SHA256
`07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`.
The private Approach startup advanced past SetPriorityClass636 and stopped at
missing PMWINX2073. No original application files were changed.

## Evidence and ABI

Installed os2-re-module catalogue, source declarations and exact-hash analysis
were reused. Permanent Ghidra analysis ID
`ad985a9fc408fd5711f958f14f699b33664c183e3269639d0fc4bd876e11dd51`
contains the encoded exports and recovered wrapper/helper bytes. Its global
Windows/stdcall model is not ABI authority. Raw near RET and public stack loads,
plus `_System` declarations in `sdk/os2tk45-master/h/os2win.h`, establish:

| Ordinal | Export | Public argument bytes | Caller cleanup | Wrapper | Helper |
|---|---|---:|---|---|---|
| 2073 | MultiByteToWideChar | 24 | yes | 0x9c4a8, 41 bytes | 0x492b4 |
| 2079 | WideCharToMultiByte | 32 | yes | 0x9b078, 49 bytes | 0x48dd0 |

The internal helpers use a different register/stack convention; that convention
is not exposed as the public API. The wire WCHAR is exactly two bytes.
Original validation helpers at0x4292c and0x427ec establish flags1004,
invalid parameters87, short output122 and default text errors1359. Forward
strict invalid characters return1113. Original reverse allowed flag mask0x270
excludes SDK WC_DEFAULTCHECK0x100; modifiers require COMPOSITECHECK0x200 and
DISCARDNS/DEFAULTCHAR cannot both be set. Original default substitution is a
NUL-terminated string of1–13 bytes, not an unbounded byte pointer. Import
fixups identify UCONV2 for UniUconvToUcs and LIBUNI36 in decomposition handling.

## Implementation and supported scope

`os2libs/pmwinx/unicode.c` implements counted CP437, CP850, CP1252/1004 and
UTF8 conversion, using explicit unaligned-safe UTF16 loads/stores. CP0/CP1
resolve through existing GetACP/GetOEMCP. Existing clipboard SBCS tables moved
unchanged into `hal/host_codepages.h`; all384 values were compared against HEAD.
Count−1 includes the NUL; positive counts preserve embedded NUL. Size queries,
exact capacities, supplementary pairs, invalid UTF8 replacement/strict failure,
reverse substitutions and usedDefault DWORD are implemented. Two passes validate
and count before output writes. Success preserves last-error state.

Composition/decomposition, glyph conversion, unsupported DBCS/codepages and
reverse normalization flags return explicit ERROR_NOT_SUPPORTED50. This is not
a claim of complete UCONV or normalization support. UTF8 PRECOMPOSED1 and an
explicit UTF8 default substitution are also unsupported. Resource policy limits
input/output to16MiB wire bytes. Public bridges snapshot all guest inputs and
validate output/optional BOOL spans, overlap, address wrap and NUL termination.
Capacity-zero output is ignored, including when equal to source.

## Verification

Native unit fixture covers every SBCS byte, ACP/OEM selection, exact/short
buffers, unaligned wire output, embedded NUL, surrogate pairs, malformed UTF8,
original flags/default validation, overlap and unsupported contracts. Parent's
coherent runtime build passes this fixture and clipboard checks. Synthetic LX
fixture `tests/integration/open32_unicode.py` additionally covers caller-cleaned
24/32-byte arguments, capacity-zero alias queries, poisoned/wrapping/unmapped
spans, mapped-object-end spans, unaligned optional BOOL, and unchanged buffers on
failure. Parent runs that emulation gate outside the macOS sandbox.

Scratch probes use a private COW copy under `build/lotus/approach-startup`,
current prepared runtime and dummy SDL. Original ball fingerprints are checked;
only owned test processes are stopped. Actual user GUI and application files
remain untouched. Startup reaching another ordinal is progress, not acceptance
of a functioning Approach UI.

The refreshed current-runtime probe executed ten CP0/flags0/NUL-terminated
MultiByteToWideChar calls before stopping at PMWINX423 InterlockedIncrement.
RuntimeID was `e74c1feb4746479208421e7433c7c80e02587b45602722147eaea0c33fa8c74a`.
The bounded probe lasted3.75s, exited1, and produced no preceding CPU or
MEM_BAD_POINTER failure. Original fingerprints were unchanged.
