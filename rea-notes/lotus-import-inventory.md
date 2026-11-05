# Lotus imports and reusable offline binary scanner

## Evidence and scope

Read-only census of `/Users/jimd/Documents/Lotus.2ineball`, using private source
revision `c3e92d1101e3af1b91789447ac6ea01f11d8b29f` plus the source hashes recorded
in the report. No guest code was run and no user-ball files were written. Each
scanned file has a SHA-256; manifest, CONFIG.SYS, parser, generated exports and
located implementation-source files are also hashed. Source changes after the
census require regeneration before comparing export counts.

The committed `lotus-native-apis.csv` enumerates all 1,280 used interfaces in
libraries built by 2ine, with application membership, selected providers, source
exports and implementation evidence. `lotus-native-apis.provenance.json` binds
that compact inventory to the full census and source revision. Semantic
completeness is explicitly unassessed.

Detailed census data is retained in the permanent review cache at
`/Users/jimd/src/os2/.cache/os2-reports/2026-11-05/build-lotus-import-inventory/`.
Generated build trees were removed after verification:

- `imports.json`: complete deterministic index, app membership, module/name/ordinal,
  binary hashes, source export evidence and reference-file coordinates.
- `references.jsonl.gz`: **130,557 exact encoded references**, deterministically
  compressed; SHA-256 `4fd03f4ac88afe2478e48072054c80c9fc4439deb86c2eab8f38ec190e28ed81`.
  A binary import's `{file,firstLine,count}` points into decompressed one-based
  JSONL lines. Parser APIs retain inline references.

## Census result

| Measure | Count |
| --- | ---: |
| Scanned binaries / opaque payload candidates | 411 |
| Static app-closure binaries | 49 |
| Additional verified dynamic-startup closure binaries | 9 |
| Other installed Lotus/plugin closure binaries | 353 |
| Distinct imports, all libraries | 10,731 |
| Distinct imports in libraries built by 2ine | 1,280 |
| Distinct imports in currently selected native 2ine libraries | 1,225 |
| Selected-native imports without matching source export | 412 |
| Incomplete import decodes among recognized images | 0 |
| Corrupt export-name metadata | 1 |
| Unsupported opaque payloads | 1 |

All-library-built-by-2ine missing count is 464; this includes 51 genuine SOM
imports and one genuine HELPMGR import whose empty/fallback native modules are
not selected providers. They must not be presented as 52 immediately missing
native interfaces. DOSCALLS #295 `Thk32R3DS` is supplied specially by
`loader/imports.c`; it is classified as a loader-special export rather than an
absent generated function. PMWINX has 627 distinct targets, 253 absent exports
in this snapshot, after the five accelerator-family exports were added.

| App | Static closure | Previously observed additional startup roots | Selected-native targets / absent exports |
| --- | ---: | --- | ---: |
| 1-2-3 | 3 | MAIN123W.DLL, closure 23 | 807 / 182 |
| Approach | 31 | None inferred | 925 / 225 |
| Freelance | 13 | FLWRSEN.DLL and FLWRS8EN.DLL | 741 / 139 |
| Organizer | 29 | OR1C30EN.DLL | 905 / 216 |
| Word Pro | 6 | None inferred | 267 / 9 |

These are overlapping sets, not additive totals. MAIN123W has the recorded user
startup-chain provenance from `lotus-123-startup.md`; Organizer/Freelance roots
have the recorded runtime-load provenance from `lotus-startup-exports.md`.
The report preserves these provenance records and source hashes separately from
ordinary static reachability. Other installed modules remain a conservative
optional superset; computed loads and import execution frequency are unknown.

405 binary import target sets matched the existing `lx_dump` decoder. Five
forwarder-bearing DLLs were deliberately not passed to it because its forwarded
bundle cursor does not advance. Forwarders are covered by independent fixtures.
Cross-check evidence was retained only for unchanged binary SHA and unchanged
target set. `INIEDIT.EXE` has complete import fixups but a malformed counted
nonresident export-name area; its export metadata remains incomplete.
`FLG/LTFLEG10.DLL` begins with a proprietary FREELANCE payload rather than an
executable header and remains explicitly unsupported.

Export presence, an ABI policy entry, a located body, and markers such as FIXME
or UNSUPPORTED are separate source evidence. None establishes correct or complete
API semantics. Unlocated bodies may be macros or wrappers, so they are not
classified automatically as stubs. The census is a prioritization input for
runtime probes and accurate implementation.

## Reusable scanner

`tools/os2binary.py` is standalone Python standard-library code. An identical
copy is staged at `build-os2-api-catalogue/stage/os2binary.py` for the portable
catalogue/skill package. It does not require a ball, 2ine runtime, Ghidra, compiler,
network connection or proprietary payload bundled with the tool.

```python
from os2binary import inspect_file, parse_image, parse_export_table, scan

metadata = parse_image(binary_bytes)
exports = parse_export_table(binary_bytes)  # independent of ordinary fixup walk
metadata = inspect_file(path, exports_only=False)  # adds SHA-256 and size
report = scan([path_or_directory], catalogue_dict, include_unknown=True)
```

```sh
python3 tools/os2binary.py /path/to/files --catalogue os2-api-catalogue.json.gz --output scan.json
python3 tools/os2binary.py /path/to/file --exports-only --include-unknown --output exports.json
python3 tools/lotus_imports.py --ball /path/to/Lotus.2ineball --output build-lotus-import-inventory
python3 -m unittest tests.tools.test_lotus_imports -v
```

Files are recognized by magic regardless of suffix, including SYS/IFS/DMD and
extensionless kernels. Explicit unknown inputs can be retained; unknown directory
members are omitted unless requested. File size is bounded at 256 MiB, directory
entries/files at 100,000, record references at two million, archive recursion at
four. Directory symlinks are skipped. Input errors and incomplete decodes remain
in JSON; successfully decoded records preceding an error remain available.

Supported metadata:

- NE/LX/LE module tables, ordinal/name imports, forwarded exports and exact record
  offsets. LX/LE references include object/page, encoded source offsets and
  preferred source addresses. NE includes segment/file offsets and qualified
  source-chain expansion. Preferred addresses are not observed runtime addresses.
- NE/LX/LE exports: resident module name, export ordinal/name, object or segment,
  entry offset and record location; LX call gates and forwarders. Internal entry
  fixups retain their local entry ordinal and decoded target export. Export-only
  mode skips ordinary relocations. `targetOSCode` is retained: target-2 NE files
  can be enumerated without assigning their exports to the OS/2 namespace.
  `importsComplete` and `exportsComplete` distinguish phase coverage.
- PE32/PE32+ normal imports, delay descriptors, named/ordinal thunks and export
  forwarders, with file/RVA/preferred-IAT positions and optional hints.
- OMF IMPORTDEF/EXPDEF records, standard page-aligned F0 libraries, EXTDEF and
  PUBDEF/PUBDEF32/LPUBDEF symbols. Static-object externals and public symbols have
  `moduleUnknown:true` and are never assigned a guessed DLL or ordinal.
- IBM F3 indexed static libraries: bounded validated name/member index traversal;
  every indexed member must begin with an OMF object header. Opaque ancillary
  header/hash fields are explicitly warned about. No magic-hunting for members.
- ar archives, COFF short import objects and normal COFF external/public symbols;
  archive member and absolute container offsets are both retained.

`lookup(catalogue,module,identifier)` handles ordinal/name/alias evidence and
name-only symbols. `scan` attaches catalogue matches with `resolutionStatus`:
`known`, `unknown`, `ambiguous`, or `known-ordinal-unnamed`. Version-conflicting
names and unqualified original export ordinals remain explicit. They do not
receive guessed unique names, calling conventions or prototypes.

## Verification and limits

21 focused regression tests pass, including isolated `python -I` execution from
a copied standalone file, gzipped catalogue loading, target-neutral exports,
corrupt metadata, exact offsets, static/dynamic membership, read-only fixture
checks and byte-identical deterministic output. Ruff and ty pass for all three
new Python files.

Real probes include NE HPFS.IFS/CDFS.IFS/OS2CDROM.DMD, LX JFS.IFS, and original LX
OS2KRNLI/OS2KRNLB/os2krnld. All three uncompressed kernels decode successfully;
raw OS2LDR and compressed `kernel.$$$` package payloads remain unsupported.
Toolkit 4.5 PMWINX.LIB yields 752 import definitions without decoder errors or
warnings. Four genuine F3 libraries yield 144 external and 87 public symbol
records, zero proven IMPORTDEFs, with the expected opaque-ancillary-fields
warning. PE32/64 fixtures exercise normal/delay/forwarded import layouts; no real
PE binary was identified in the bounded Toolkit 4.5 executable sample inspected.

Known gaps are explicit: NE zero-size relocation segments; compressed/iterated
payload materialization and LX internal source-chain expansion; opaque future
parameter-type metadata; COFF bigobj and unknown vendor object/container
extensions; raw boot images and still-compressed install packages. Static
metadata cannot enumerate computed dynamic loads or prove runtime API behavior.
None of those limits is silently converted into a complete import inventory.

Format references: [IBM OMF/LX revision 8](https://bitsavers.org/pdf/ibm/pc/os2/OS2_OMF_and_LX_Object_Formats_Revision_8_199406.pdf),
[TIS OMF 1.1](https://openwatcom.org/ftp/devel/docs/omf.pdf),
[Microsoft PE/COFF](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format),
and the existing repository `formats`, `loader/load_lx.c`, `loader/load_ne.c`, and
`tools/lx_dump.c`. The F3 index has bounded observed-format provenance rather than
an invented claim of a complete vendor specification.


## Foreign target-domain extension

`parse_image` and `inspect_file` now accept `allow_foreign=True`; `scan` accepts
that flag plus an explicit `domain`. Default OS/2-only parsing remains unchanged.
Foreign structural NE/LX/LE references retain encoded target OS and domain, and
nested archive references preserve that context. Catalogue domain mismatch yields
`api:[]` and `resolutionStatus: unbound-domain-mismatch`; selecting a namespace
never relabels a foreign binary. `os2api --domain win16 scan` opts into structural
Win16 decoding and binds only the selected Win16 catalogue. Module metadata uses
neutral structural decoding without binding catalogue names.

Actual target-2 COMMDLG.DLL decoded 150 imports/48 exports and WIN87EM.DLL decoded
3/5, both complete without warnings. Injected OS/2 ordinal collisions were rejected
for every import. The explicit Win16 CLI decoded COMMDLG with two known imports
from its current catalogue and left the remainder unknown. Evidence is under
`build-os2-module-cache/real-win16-*.json`; this is encoded target provenance, not
a claim every target-2 device module executes as a Windows application.
