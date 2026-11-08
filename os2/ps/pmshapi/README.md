<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# PMSHAPI.DLL

Shell API: profiles (Prf*), switch list and WinStartApp.

- IBM source: `E:\w45ps\src\ps\ishapi\` (readini.c, memory.c), linked into
  PMMERGE; the ishapi = PMSHAPI link is inferred.
- Evidence: `@BSS` labels in `pmmrggre.sym`; LX entry table of PMSHAPI.DLL
  (inventory `modules.json`, 8ffa3212e0b0…).
- In Warp 4.52: Forwarder only: every export forwards to a PMMERGE ordinal.
- In 2ine: Forwarder only, as in Warp. The shell API is implemented in
  `os2/ps/pmmerge/ishapi/`.
- Exports: `pmshapi.def`.
