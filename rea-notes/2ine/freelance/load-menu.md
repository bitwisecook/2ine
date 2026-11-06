# Freelance LoadMenu

The private, disposable Freelance startup progressed through PMWINX.262,
PMWINX.606 and PMWINX.306, then stopped at PMWINX.462. The actual Lotus ball
was read and copied; it was never launched or changed.

The installed os2-re-module skill/catalogue identifies ordinal 462 as
`HMENU WINAPI LoadMenu(HINSTANCE, LPCSTR)`. The original PMWINX provider is
SHA-256 `07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655`.
Its existing permanent full analysis
`ad985a9fc408fd5711f958f14f699b33664c183e3269639d0fc4bd876e11dd51`
retains the original function bytes. No new target analysis was required.
The public wrapper at preferred address 0xa1504 reads module and resource
from entry ESP+4/+8, normalizes module -1 to 0, and returns with plain RET:
argument_bytes=8, callee_pop_bytes=0. The helper's Optlink register use is
internal and does not establish the public convention.

The helper at 0x38590 resolves native resource type 3, then calls the encoded
PMWIN.778 WinLoadMenu with HWND_OBJECT (2), module and resolved numeric ID.
The helper at 0x35ca0 accepts low-word resource IDs, `#decimal` strings, or
looks up names using DosGetResource(module, RT_RESNAMES=255, nativeType=3).
That table starts with a DWORD count, followed by that many DWORD IDs and
then sequential NUL-terminated names. The SDK additionally defines
RF_ORDINALID=0x80000000. Our names compare ASCII case without altering high
bytes. Full original DBCS collation and permissive legacy atoi edge cases
remain outside this bounded implementation.

Actual Freelance FLWRSEN.DLL is SHA-256
`7dac7624c59c58885945cc457bddd15e369216c7334f7ed424c22113c9d68a42`.
Its type-3 resources 1, 3, 4 and 5 are native PM MENU templates, not PE menu
resources. Resource 1 contains 120 nested menu nodes and 940 items, using
only text, separator and submenu styles; all 120 nodes declare codepage 437.
The 17,842-byte resource hash is
`5018d54aece006d7cee93154d97ebc99e3f8d8f317e6bdd0d74502629ccdd065`.
Static extraction used the existing LX page decoder without executing target
code. Native templates already have a checked parser; LoadMenu reuses it.

The Open32 adapter preserves the object-parented root and makes every submenu
queryable. It translates codepage/mnemonics/shortcut text into Open32 text,
retains checked/disabled/highlighted flags, and normalizes PM shortcut separators
for the native renderer. A private pending list is published only when the
whole hierarchy is represented; failure destroys the native owner tree and
frees every pending metadata record. Destroy/Delete prune records for destroyed
children; Remove keeps the detached submenu alive. Loaded roots can be attached
with SetMenu from HWND_OBJECT, matching the original load/attach sequence.
Provider codepage is retained on the internal native menu rather than guessed
from the process when converting resource text.

The native regression exercises numeric, #ordinal and case-insensitive named
lookup, nested text/state/codepage conversion, object parent, submenu deletion
and detachment, and malformed resource/name-table cleanup with resource counts
balanced. The generated LX fixture checks the real guarded bridge with
caller-cleaned ESP, nested queries, named/numeric IDs, attachment to a hidden
Open32 window, and complete descendant destruction. Tests use SDL dummy only;
no real desktop input or user-ball writes occur. Focused execution results and
the next original-program blocker are recorded in the evidence receipt after
the coordinated build. Working menus and a rendered Freelance UI are separate
acceptance gates; this change alone does not claim either complete application.

The initial coordinated native gate could not execute: an existing-target
rebuild reached the PMWIN link and failed on unrelated external additions
(WinBroadcastMsg, cleanupErrorInfo, destroyStandardFont and tb_icon_executable).
The new menu source and native regression compile with -Wall -Wextra -Werror;
Ruff and ty checks pass for the generated LX test. Neither runtime test nor
an original Freelance post-462 startup run is claimed yet. The current proof
scratch remains under build/lotus/freelance-menu until that integration gate;
the disposable startup ball remains an active next-probe input. Completed
pointer and metric scratch has been removed after permanent-cache/report intake.

The exact source boundary, pending central registrations, active scratch/process
ownership and suggested continuation prompt are in `load-menu-handoff.md`.
