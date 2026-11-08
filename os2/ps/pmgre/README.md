<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# PMGRE.DLL

Graphics engine entry points (Gre32Entry*).

- IBM source: `E:\w45ps\src\ps\pmgre\`, `…\pmgre\386\`, linked into PMMERGE.
- Evidence: `@BSS` labels in `pmmrggre.sym`; LX entry table of PMGRE.DLL
  (inventory `modules.json`, 005f4b594723…).
- In Warp 4.52: Forwarder only: every export forwards to a PMMERGE ordinal.
- In 2ine: Forwarder only, as in Warp. The engine entries are implemented in
  `os2/ps/pmmerge/pmgre/` (PMMERGE 5050 and 5052).
- Exports: `pmgre.def`.
