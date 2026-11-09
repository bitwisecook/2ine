<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# DIVE.DLL

Direct interface video extensions (Dive*).

- IBM source: `mm\video\dive\`; only C runtime units are named.
- Evidence: SYM directory `mm/video/dive` (`dive.sym`, and a `FWDDLL.sym`
  that the shipped DIVE.DLL does not use); LX entry table of DIVE.DLL
  (inventory `modules.json`, 95a10b6c2a98…, BLDLEVEL `@#IBM:14.075#@`): 20
  code exports, no forwarders; MMPM does not export Dive*.
- In Warp 4.52: Real code, a DLL of its own.
- In 2ine: Real code, as in Warp; ordinals 1-19 match Warp's names (Warp's 20
  is `_DIVEInitTerm`, the DLL initialisation routine). The window targets DIVE
  blits into belong to PMMERGE (`os2/ps/pmmerge/pmwin/dive_targets.c`).
- Exports: `dive.def`.

## Units

| 2ine unit | APIs and services |
|---|---|
| `module.c` | Dive* entries: instances, image buffers, blitter setup, palettes, blits |
| `dive_pixels.c`, `dive_pixels.h` | FOURCC layouts and YUV/RGB conversion |
| `dive_palette.h` | The IBMVID 256-colour default palette |
