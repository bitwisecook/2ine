<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# SESMGR.DLL

Session manager (DosStartSession, DosSelectSession, …).

- IBM source: Unknown.
- Evidence: LX entry table of SESMGR.DLL.
- In Warp 4.52: Forwarder only, to DOSCALLS/DOSCALL1.
- In 2ine: Forwarder only, as in Warp. The sessions are implemented in
  `os2/baseos/doscalls/doscall1/session.c` (DOSCALLS 669–672, 690, 693, 699,
  702, 712).
- Exports: `sesmgr.def`.
