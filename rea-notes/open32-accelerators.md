# Open32 accelerator resources and ordinal coverage

## Inventory and original evidence

This change closes the coherent PMWINX accelerator family:

| Ordinal | API | Stack argument bytes |
| --- | --- | --- |
| 57 | CreateAcceleratorTable | 8 |
| 141 | DestroyAcceleratorTable | 4 |
| 457 | LoadAccelerators | 8 |
| 697 | TranslateAccelerator | 12 |
| 1026 | CopyAcceleratorTable | 12 |

All use caller-cleaned 32-bit Open32 linkage. IBM Toolkit 4.5 `h/os2win.h`
declares the signatures and six-byte ACCEL record (BYTE flags, padding, WORD key,
WORD command). `lib/pmwinx.lib` OMF import records and the selected original
PMWINX export/name tables establish the ordinal identities.

Selected original:
`/Users/jimd/src/os2/extracted/install-media/os2image/FI/DAX/DLL/PMWINX.DLL`,
SHA-256 `07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`.
Object 1 export offsets are 57=616248, 141=600568, 457=600440, 697=610480,
1026=600612. Original LoadAccelerators resolves resource type 8 and calls the PM
accelerator loader; it does not parse a PE accelerator resource. REA evidence
`ev_1809fc38af73b530ef642504afba4ff33af090455a9e3e25a66a514b113120fa`
is retained in `build-open32-accelerators-current/load-rea.json`, alongside
unpacked export slices and direct original disassembly. The raw slice has
unapplied relocations, so imported call identities are checked against the
module fixup/import catalogue rather than inferred from placeholder addresses.

The read-only installed Lotus tree contains 18 type-8 accelerator resources in
8 DLLs, all codepage 437. Real examples include `FLG/flwrsen.dll` IDs 1/2/3
(55/2/6 entries), `APPROACH/appresen.dll` IDs 28992/30000 (40/115 entries),
`WORDPRO/wpeno70.dll`, Organizer and shared chart/editor DLLs. Their wire layout
is USHORT count, USHORT codepage, followed by count six-byte PM records
(USHORT flags, key, command). Real entries combine AF_CHAR|AF_VIRTUALKEY; treating
those flags as mutually exclusive would reject genuine Lotus tables.
Decoded metadata and table evidence are private under
`build-open32-accelerators-current/{lotus-resources,decoded-resources}.json`.

The first direct-EXE census found Approach 402 PMWINX targets / 114 absent,
Freelance 406 / 114, Organizer 1 / 0, 1-2-3 12 / 2, WordPro 8 / 1. Both Approach
and Freelance directly import #457 and #697, now implemented. The other three
family members occur in the broader shared-DLL inventory. The full inventory
agent parsed 411 binaries without parser errors, finding 627 PMWINX targets,
258 absent before this five-export change. Static import presence does not prove
that an application executes every import. Exact importers and classifications
are in `build-lotus-import-inventory/imports.json`; the inventory tool owns its
updated final counts. The bounded main-EXE comparison is retained under
`build-lotus-pmwinx-current/`.

## Implementation and ownership

`pmwinx/accelerators.c` parses checked OS/2 accelerator resources and copies their
contents before releasing the loader resource. It also accepts Windows ACCEL
arrays, preserving Windows virtual/character-key and modifier semantics. Copy
reports entry counts, clips writes to the requested entry capacity, and emits
canonical padding. Malformed resources, invalid ranges, bad guest buffers and
invalid typed handles return errors. Invalid accelerator handles report 1403.

The process owns typed OPEN32_ACCEL handles (enum appended without renumbering
existing kinds). Tables are immutable and can be used across host/guest threads.
Their lifetime is independent of the source resource or a PM anchor/video
context. Destroy releases ownership through the shared handle registry, while
in-flight copies retain a borrow. Native-module shutdown closes all remaining
tables and releases the monitor at a quiescent point. No SDL initialization is
required to load, create, copy or destroy a table.

Translate matches virtual, character and system-key messages with current
Open32 modifier state. It sends WM_COMMAND or a resource's WM_SYSCOMMAND
synchronously, tagging the high word of wParam as accelerator-origin (1), and
leaves the caller's MSG unchanged. It releases the table lease before calling
original guest code, allowing that callback to destroy the table. Disabled
windows do not receive translated commands.

This implements numeric LX resource IDs and `#decimal` spelling. General named
resource lookup is still unsupported by the loader's resource interface and
fails explicitly. The character converter supports the existing runtime's
437/850/1004 codepages; zero codepage uses its OEM default. AF_HELP translation
is explicitly unsupported, and full menu initialization/highlight/disabled-item
semantics are not claimed. The implementation does not equate an opaque Open32
HACCEL with a native PM HACCEL. No new unrelated ordinal stubs were added.

Windows API contracts used for copy/create/synchronous delivery and errors:
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createacceleratortablea
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-copyacceleratortablea
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-translateacceleratora
https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--1300-1699-

## Validation

Focused tests pass:
- `open32_accelerators`: real generated LX resource table, malformed resource,
  ACCEL array creation and checked partial copy with guard bytes, invalid guest
  pointers/counts, virtual and character keys, real PM Ctrl-state input,
  synchronous COMMAND and SYSCOMMAND callbacks, unchanged MSG, disabled target,
  and reentrant table destruction from the command callback.
- `open32_accelerator_ownership`: resource-byte independence, process-wide table
  access from a second host thread, typed-handle rejection after destruction,
  malformed input, and actual native-module shutdown/reinitialization.
  SDL_WasInit(SDL_INIT_VIDEO) stays zero for the whole resource-only test.
- `source_boundaries`.

The build uses official SDL 3.4.18 sources, hash verified as
`9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3`.
The old system-provider diagnostic cache could not configure after Homebrew
changed to SDL 3.4.20; no exact-version policy was weakened. Private build:
`build-lotus-coverage/runtime`. OpenGL remains off as in the receiving release
profile. Pinned clang-format 21.1.8, Ruff and ty pass for the changed sources.
Native i386 execution is covered by the generated fixture's native backend but
was not available on this ARM64 host.

A disposable COW clone runs the original Freelance EXE, SHA-256
`4d5a3b5ac9df801c4ec584801b0f1b313b55e27d69dd5e7c65e1c1d0cfd30cb1`,
with dummy video and a standalone deterministic 20-second watchdog. It executes
LoadAccelerators(65775,1) and LoadAccelerators(65775,3), reads the real language
DLL resources without a parse error, and reaches missing PMWINX #262
(GetCursorPos). There is no MEM_BAD_POINTER or video failure. This is startup
progress, not interactive application acceptance or proof of physical keyboard
translation in the original GUI. Result/log are retained under
`build-open32-accelerators-current/original-freelance/`.

The old runtime-linked diagnostic write interposer crashed before any guest
trace after the runtime upgrade. Rebuilding it did not change that; substituting
a standalone watchdog without a GLoaderState dependency restored a valid probe.
No product workaround was added for the obsolete diagnostic hook. The installed
user ball was read only; no user application was launched, no guest bytes were
patched, and no desktop/clipboard input was used.
