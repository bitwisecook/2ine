<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# MSG.DLL

Message files (DosGetMessage, DosInsertMessage, DosPutMessage).

- IBM source: Unknown; `msgseg.asm` and `qryseg.asm` are static stubs in the DDK `doscalls.lib`.
- Evidence: LX entry table of MSG.DLL; `doscalls.lst`.
- In Warp 4.52: Forwarder only, to DOSCALLS/DOSCALL1.
- In 2ine: Forwarder only, as in Warp. Message retrieval and output are
  implemented in `os2/baseos/doscalls/doscall1/msg.c` (DOSCALLS 383, 384,
  386–388).
- Exports: `msg.def`.
