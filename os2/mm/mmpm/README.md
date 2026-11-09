<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# MMPM.DLL

The merged MMPM/2 library: the media control interface (MDM) and its
waveaudio, sequencer and digital-video devices.

- IBM source: no build directory of its own is known. The SYM directory
  `mmos2/dll/merge` lists the components linked into it: ampmxmcd, audioif,
  audiomcd, audiosh, fssh, hhp, mdm, midiio, midimcd, mipm, mish, mmio, ssm
  and waveproc (`mmos2/dll/nonmerge` holds the same names built separately).
- Evidence: LX entry table of MMPM.DLL (inventory `modules.json`,
  07dd55b67734…, BLDLEVEL `@#IBM:14.075#@`) and of the MDM forwarder;
  `F:\w4mm\src\mm\mci\mdm\*.c` strings in the unpacked MMPM.DLL.
- In Warp 4.52: Real code; MDM and MMIO forward to it.
- In 2ine: Implements the MDM entries at Warp's MMPM ordinals (1-3).
- Exports: `mmpm.def`.
