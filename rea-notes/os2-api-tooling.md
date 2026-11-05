# Shared offline API and module evidence tooling

The workspace's shared os2-re skill now contains a portable Python catalogue and
binary scanner, with entrypoints `scripts/os2-apis` and `os2-re{,-module,-driver}
api`. Lookup and static scans do not execute the input, fetch media or change
the user's ball. This is analysis tooling, separate from ordinary cached 2ine
launch dependency status.

The packaged catalogue combines 51 SDK libraries and 873 original OS/2 binary
variants: 48,463 ordinal keys in 516 ordinal namespaces plus one by-name-only
module record. A separate Win16 domain holds 6,837 ordinal keys in 200 modules.
Unknown-target images remain quarantined. It retains 26,969 conditional SDK/DDK
declaration variants, 25 convention profiles, hash-bound source references and
stored manual excerpts. Names alone do not establish callable ABI or semantic
implementation. Conditional prototypes and mixed-header layout caches remain
unqualified, and actual version conflicts require a selected provider hash.

Portable files are under
`/Users/jimd/src/os2/tools/src/workspace-skill/os2-re/scripts`, mirrored to the
installed `.agents/skills/os2-re/scripts`. Source/metadata lookup uses the
packaged deterministic gzip data without the original SDK/manual trees. The
catalogue's gzip SHA-256 is
`2874e89b6c0daf3e01c7d757eb147829efd9863087db329e0cbd16c10bd9d445`.
Its decompressed JSON SHA-256 is
`871abcbd3f2a7b6adabe630df4960b4bee6fb10327d604795b330ef1a626de6c`.

The nested retrokit Ghidra extension resolves qualified imported ordinals,
preserves encoded name-versus-ordinal roles and analyst annotations, labels
known unnamed exports, records conflicts and leaves ambiguous/unnamed bindings
unresolved. Automatic OS/2 annotations require an actual OS/2 image. Windows
namespaces do not inherit OS/2 declarations. Executable LE/LX object flags now
select 16/32-bit decoding before disassembly; compiler selection remains an
analysis model rather than proof of the input compiler.

`LookupOS2Api.java`, `AnnotateOS2Apis.java` and
`InspectOS2CallingConventions.java` provide offline queries, metadata refresh
and decoded return/call/stack observations. Observed return forms do not alone
establish parameter order, storage, variadic or floating-point rules.

The Lotus-specific census and its source export comparison are described in
`lotus-import-inventory.md`; ordinal 636's original evidence is in
`lotus-priority-api.md`. The census exposes the missing interfaces together;
the catalogue and Ghidra annotations do not implement them. The accelerator
family's bounded implementation and remaining limitations are documented in
`open32-accelerators.md`.

Qualification output is retained under `build-os2-api-catalogue`,
`build-os2-api-plugin` and `build-os2-api-integration` in the private release
worktree. These checks qualify offline lookup and annotations, not full Lotus
application compatibility or a reconstructed kernel.

## Reusable DLL analysis and Markdown sources

`scripts/os2-modules` and `os2-re-module` now expose a shared immutable cache.
Static identities are keyed by SHA-256; filenames, encoded version/resource,
linker and translator metadata remain searchable aliases/evidence. Compiler
string clues are qualified separately. An exact cache hit avoids re-extraction.
A Ghidra request additionally selects the exporter/model profile; matching saved
analysis is returned before resolving Ghidra. Refresh retains prior revisions.

The exporter records encoded public interfaces, recovered internal functions,
parameter/return storage, calling convention observations, complete type graphs
and qualified code/CFG features. Each function records its actual 16/32-bit
decode context and near/far/interrupt return observations. Partial bodies and
unknown private coverage stay explicit. Similarity ranking compares compatible
model/method features and explains its reasons; it never applies names or types.

`docs import`, `docs from-catalogue`, `docs list`, `docs search` and `docs get`
associate immutable Markdown with exact DLL hashes, module-qualified APIs,
source hashes, selected pages/sections and renderer provenance. Common pages
share content; readable per-DLL/per-name indexes link to their pages and source
records. Existing converted manuals can be cached page by page with `--all-pages`.
Full pages, whole documents, excerpts, SDK declarations and analysis renderings
retain separate coverage labels. PDF import uses existing converted Markdown;
no implicit network lookup or PDF conversion runs during lookup.

Canonical instructions:
`/Users/jimd/src/os2/tools/src/workspace-skill/os2-re/references/module-cache.md`.
Installed instructions and implementation are mirrored under
`/Users/jimd/src/os2/.agents/skills/os2-re`. The cache defaults to
`OS2_MODULE_CACHE` or `~/.cache/os2-re-module`; `--cache-root` selects a private
cache. This data is separate from 2ine's application-launch dependency index.

Qualification includes 54 cache/metadata/docs/router tests, 15 API catalogue
checks, genuine OS/2 and Win16 input, mixed 16/32-bit fixtures, original NE driver
export, repeated deterministic analysis and reuse with an unavailable provider.
The module receipt and native Ghidra evidence are retained in
`build-os2-module-cache` in the private worktree. Original source images and user
balls were not modified.
