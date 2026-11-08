<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# QUECALLS.DLL

Queues (DosCreateQueue, DosReadQueue, …).

- IBM source: Unknown.
- Evidence: LX entry table of QUECALLS.DLL (inventory `modules.json`).
- In Warp 4.52: Forwarder only: every export forwards to a DOSCALLS ordinal implemented by DOSCALL1.
- In 2ine: Forwarder only, as in Warp. The queues are implemented in
  `os2/baseos/doscalls/doscall1/queue.c` (DOSCALLS 661–668, 674, 676, 681).
- Exports: `quecalls.def`. QUECALLS.3 is DOSCLOSEQUEUE (DOSCALLS.676), as in
  Warp and the Toolkit's `bseord.h`; QUECALLS.2 is DOSPURGEQUEUE, which 2ine
  does not implement.
