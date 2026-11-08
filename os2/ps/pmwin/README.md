<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# PMWIN.DLL

Window manager API: windows, messages, input, painting, dialogs, menus and the WC_* controls.

- IBM source: `E:\w45ps\src\ps\pmwin\` (winpos.c, winloop.c, winpres.c, …),
  linked into PMMERGE.
- Evidence: `@BSS` labels in `pmmrggre.sym`/`PMMERGE.SYM`; LX entry table of
  PMWIN.DLL (inventory `modules.json`, 7988bfdd400e…).
- In Warp 4.52: Forwarder only: every export forwards to a PMMERGE ordinal.
- In 2ine: Forwarder only, as in Warp. The window manager is implemented in
  `os2/ps/pmmerge/pmwin/`. The 32-bit entries are nameless in both PMWIN and
  PMMERGE; the PMMERGE ordinals follow the alphabetical order of the API
  names, which matches every forwarder in `pmwin.def`.
- Exports: `pmwin.def`.
