<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# PMCTLS.DLL

Standard controls and dialogs: file and font dialogs, notebook, value set, slider, spin button and container registration.

- IBM source: `E:\w45ps\src\ps\controls\pmctls\` (ctldbg.c) with siblings `cnr`, `slider`, `dial`, `value`, `notebook`, `dialogs`, `drag`, `mle`.
- Evidence: `@BSS` labels in `PMCTLS.SYM`; SYM directories `ps/controls/*`; LX
  entry table of PMCTLS.DLL (inventory `modules.json`, 7fe701dc2559…).
- In Warp 4.52: Real code; implements the PMDRAG and PMMLE exports.
- In 2ine: Implements the file and font dialogs (`dialogs/`) and drag and
  drop (`drag/`); PMDRAG forwards to it.
- Exports: `pmctls.def`.

## Units

| 2ine unit | IBM unit | APIs |
|---|---|---|
| `module.c` | `pmctls\` (entry) | DLL entry and export table |
| `dialogs/filedlgm.c` | `dialogs\filedlgm.c` (inferred: the only known file-dialog unit) | WinFileDlg, WinDefFileDlgProc, WinFreeFileDlgList |
| `dialogs/fontdlg.c` | unknown (descriptive) | WinFontDlg, WinDefFontDlgProc |
| `drag/drgutil.c` | `drag\drgutil.c` | Drg* string handles, DRAGINFO/DRAGTRANSFER, RMF, transfer messages, DrgGetPS, DrgDrag |
