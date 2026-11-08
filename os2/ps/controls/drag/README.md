<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# PMDRAG.DLL

Direct manipulation (Drg*).

- IBM source: `E:\w45ps\src\ps\controls\drag\` (drglazy.c, drgutil.c), linked
  into PMCTLS.
- Evidence: `@BSS` labels in `PMCTLS.SYM`; SYM directory `ps/controls/drag`;
  LX entry table of PMDRAG.DLL (inventory `modules.json`, 2b3e0a7e8276…).
- In Warp 4.52: Forwarder only: every export forwards to a PMCTLS ordinal.
- In 2ine: Forwarder only, as in Warp. Drag and drop is implemented in
  `os2/ps/controls/pmctls/drag/`.
- Exports: `pmdrag.def`.
