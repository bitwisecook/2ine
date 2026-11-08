<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# MSG.DLL

Message files (DosGetMessage, DosInsertMessage, DosPutMessage).

- IBM source: Unknown; `msgseg.asm` and `qryseg.asm` are static stubs in the DDK `doscalls.lib`.
- Evidence: LX entry table of MSG.DLL; `doscalls.lst`.
- In Warp 4.52: Forwarder only, to DOSCALLS/DOSCALL1.
- Exports: `msg.def`.
