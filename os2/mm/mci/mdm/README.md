<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# MDM.DLL

Media control interface (mciSendCommand, mciSendString, mciGetErrorString).

- IBM source: `F:\w4mm\src\mm\mci\mdm\` (mmsndcmd.c, mmsndstr.c, minit.c, …),
  linked into MMPM; this directory's `FWDDLL` is the forwarder.
- Evidence: Strings in the unpacked MMPM.DLL; `mm/mci/mdm/FWDDLL.sym`; LX
  entry table of MDM.DLL (inventory `modules.json`, 082794dc38c3…).
- In Warp 4.52: Forwarder only: all 18 exports forward to MMPM ordinals.
- In 2ine: Forwarder only, as in Warp, for the three entries 2ine implements
  (MDM 1-3 -> MMPM 1-3). The media control interface is implemented in
  `os2/mm/mmpm/mci/`.
- Exports: `mdm.def`.
