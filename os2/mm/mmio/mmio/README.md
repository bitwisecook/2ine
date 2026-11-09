<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# MMIO.DLL

Multimedia file I/O (mmio*).

- IBM source: `mm\mmio\mmio\`, linked into MMPM; file names unknown. This
  directory's `FWDDLL` is the forwarder.
- Evidence: SYM directory `mm/mmio/mmio` (`FWDDLL.sym`, `MMIOI.sym`,
  `mmio.sym`); LX entry table of MMIO.DLL (inventory `modules.json`,
  21271289115d…).
- In Warp 4.52: Forwarder only: all 105 exports forward to MMPM ordinals.
- In 2ine: Forwarder only, as in Warp, for the seven entries 2ine implements
  (MMIO 26-30, 32, 77 -> MMPM 39-43, 45, 90). The file I/O is implemented in
  `os2/mm/mmpm/mmio/mmio/`.
- Exports: `mmio.def`.
