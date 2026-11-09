<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# MMPM.DLL

The merged MMPM/2 library: the media control interface (MDM) with its
waveaudio, sequencer and digital-video devices, and multimedia file I/O
(MMIO).

- IBM source: no build directory of its own is known. The SYM directory
  `mmos2/dll/merge` lists the components linked into it: ampmxmcd, audioif,
  audiomcd, audiosh, fssh, hhp, mdm, midiio, midimcd, mipm, mish, mmio, ssm
  and waveproc (`mmos2/dll/nonmerge` holds the same names built separately).
- Evidence: LX entry table of MMPM.DLL (inventory `modules.json`,
  07dd55b67734…, BLDLEVEL `@#IBM:14.075#@`) and of the MDM and MMIO
  forwarders;
  `F:\w4mm\src\mm\mci\mdm\*.c` strings in the unpacked MMPM.DLL.
- In Warp 4.52: Real code; MDM and MMIO forward to it.
- In 2ine: Implements the MDM entries (MMPM 1-3) and the MMIO entries
  (MMPM 39-43, 45, 90) at the ordinals Warp's forwarders name.
- Exports: `mmpm.def`.
