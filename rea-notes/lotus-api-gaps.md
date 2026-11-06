# Lotus API and library gap worklist

This refresh compares all installed Lotus executable/plugin closures against a
hash-recorded snapshot of current 2ine source exports and portable ABI policies.
The original `os2-re-module` API catalogue and exact-hash module cache supply
ordinal names and declaration/document references. No guest code was executed
for this scan, no original binary was patched, and the user ball was read only.


## Implementation continuation (6 November 2026)

The frozen counts above predate the unfinished native-export implementation.
See the [current handoff](2ine/lotus-ordinals/export-batch-handoff.md) for source
coverage, outstanding SOM/process/DOS work, integration requirements and retained
RE evidence. The 393-export batch has not been built or tested. The user requires
all entries to be implemented before testing begins; earlier test results below
apply only to the preceding audit/SmartMaster work.

## Current snapshot

| Measure | Count |
| --- | ---: |
| Files inspected, including one opaque candidate | 411 |
| Distinct interfaces in selected native providers | 1,225 |
| Missing selected-native source exports | 393 |
| Export present, portable ABI policy absent | 1 |
| Missing exports associated with known 1-2-3 closures | 166 |
| Unresolved optional library names / importer references | 17 / 19 |
| Incomplete import decodes among recognized images | 0 |

The 393 gaps cover the conservative suite/plugin corpus, not one startup or a
claim that every feature needs all of them. The missing exports include
`CreateProcess` (#92) and `GetExitCodeProcess` (#1005) encoded directly in
`123W.EXE`; static import presence does not prove those branches were executed.
The portable-policy gap is `DOSCALLS:323` (`DosQueryAppType`): source export
presence alone does not establish a callable portable bridge.

| Native module | Missing source exports |
| --- | ---: |
| PMWINX | 234 |
| PMGPI | 57 |
| PMWIN | 34 |
| PMWP | 32 |
| PMDRAG | 15 |
| DOSCALLS | 10 |
| MMIO | 4 |
| PMSHAPI | 3 |
| PMCTLS | 2 |
| PMSPL | 1 |
| SO32DLL | 1 |

## Reports to use before implementing a feature

- [Missing native interfaces](lotus-missing-native.csv): complete filtered worklist,
  including importer DLLs, exact ordinal/name, application association and source evidence.
- [Portable policy gaps](lotus-portable-policy-gaps.csv): source exports lacking a
  recorded generated guest ABI policy, separate from missing export names.
- [Unresolved libraries](lotus-missing-libraries.json): optional/plugin module
  searches and importer paths. Notes, database and networking providers dominate;
  these are not all requirements of the amortization workbook.
- [All interfaces](lotus-native-apis.csv) and [provenance](lotus-native-apis.provenance.json).

The previous census already contained every missing PMWINX API encountered in
this SmartMaster investigation. Some, including nearest-colour and tabbed text
measurement, were only associated with optional shared DLLs rather than the
old 1-2-3 startup roots. The wider installed-DLL pass must remain the planning
superset; using only the small startup closure repeats the one-at-a-time failures.

Useful work groups include dialog controls/message boxes, coordinate and bitmap
operations, metafiles, file/temporary-path services, registry services, process/OLE
support, and native PM/GPI/WPS/drag interfaces. Encoded reference counts can help
order investigation, but are not observed call frequencies. Each API family
still needs its correct OS/2 declaration/ABI and meaningful contract tests;
exporting placeholder success values would not close these gaps.

## Reproduce and resolve original interfaces

```sh
python3 tools/lotus_imports.py --ball /path/to/Lotus.2ineball --output build/lotus-api-audit
```

This now automatically emits the three filtered reports as well as the complete
index and compressed exact import references. The report-filter and parser suite
passes 23 tests, including provider selection, optional candidates and portable
policy separation. Use the ordinary build-directory cleanup rules after review.

The available workspace module helper was exercised directly:

```sh
/Users/jimd/src/os2/scripts/os2-re-module api scan /path/to/123W.EXE --json
/Users/jimd/src/os2/scripts/os2-re-module interfaces 07a7368235f8b5b0b42b955dc5d8130eb0fdf178c3f20bc62ab334b1da339655 --name '#630'
```

The CLI scan's 123W SHA-256 and import set match the census. The exact original
PMWINX cache record `f32a2647e9ecc80f4f44a754011f8ba819b6b2adacefdb610d364b4038a6fbf0`
identifies ordinal 630 as `SETMETAFILEBITSEX`. Catalogue lookup resolves all 393
remaining gap identifiers; 16 have no stored signature variant. The shared
catalogue also supplies the name `GETHOSTID` for `SO32DLL:4`, absent from the older
2ine name table. Raw declaration variants, conditional layouts and cached
analysis models remain unqualified ABI evidence. Named WPS class-data exports
must not be assumed callable functions merely from an inventory label.

## Retained evidence and limits

The full immutable analysis metadata is retained at `/Users/jimd/src/os2/.cache/os2-reports/2026-11-05/lotus-api-audit-1d3e188ab579`.
It contains the complete index, compressed exact references, module-tool scan,
original #630 cache result, catalogue enrichment and test receipt. The compact
reports above bind to its hashes through the provenance file. Source files can
change after this snapshot; rerun the census before comparing new totals.

A corrupt export-name table in `WORDPRO\INIEDIT.EXE` remains explicit; its import
metadata decoded completely. `FLG\LTFLEG10.DLL` is an opaque unsupported input.
The scan cannot prove completeness for that payload, external installation
media, computed/unobserved dynamic loads, or later API behaviour. It deliberately
keeps original SOM/HELPMGR providers separate rather than reporting all of their
absent native exports as 2ine implementation requirements. An exported API with
incomplete semantics remains a separate problem from an absent ordinal.

The [SmartMaster verification receipt](lotus-smartmaster.md) records the working
loan workbook, corrected tab transitions, resize and ordinary worker exit tests.
Only small reports/screenshots and this reusable analysis-data cache are retained;
the private test runtime, copied ball and audit scratch tree were removed.
