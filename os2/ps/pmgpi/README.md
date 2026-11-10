<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# PMGPI.DLL

Graphics programming interface (Gpi*) and device contexts (Dev*).

- IBM source: Inferred `ps\pmgpi`; file names gpimeta.c, gpimtof.c and mt2*.c are evidenced.
- Evidence: Strings in the unpacked PMGPI.DLL.
- In Warp 4.52: Real code, plus one forwarder to PMMERGE.
- In 2ine: Entry points over the graphics engine in PMMERGE
  (`os2/ps/pmmerge/pmgre/`), which it links.
- Exports: `pmgpi.def`.

## Units

IBM's PMGPI unit names are known only for metafiles; the other units are
named after the Toolkit's INCL_GPI* groups.

| 2ine unit | APIs |
|---|---|
| `module.c` | Export table; GpiCreatePS/DestroyPS/Associate/QueryDevice, GpiLine/PolyLine/Move, GpiSetColor, GpiSetPel, GpiErase, log colour table, RGB/colour index, code page |
| `presentation_space.c` | GPI state of a PS, GpiQueryPS/SetPS/ResetPS |
| `state.c` | Attributes: mix, colours, line type/width/end/join, arc parameters, patterns, current position, character mode and extra, GpiSet/QueryAttrs, GpiSave/RestorePS, page viewport, GpiConvert |
| `transforms.c` | Model and view matrices, GpiTranslate/Scale/Rotate, graphics field, viewing limits |
| `correlation.c` | Pick aperture, boundary data, stop-draw and draw control |
| `color.c` | GpiQueryPel, nearest and real colours, palettes |
| `paths.c` | Paths and areas, arcs, GpiPolygons |
| `shapes.c` | GpiBox |
| `regions.c` | Gpi* regions |
| `bitmap.c` | Bitmaps, GpiBitBlt/WCBitBlt, GpiDrawBits, GpiSetBitmapBits |
| `fonts.c`, `public_fonts.c` | Logical fonts, font metrics, character sets and boxes, width tables, GpiQueryTextBox; public font loading |
| `text.c` | GpiCharString*, character angle/shear/direction, text alignment |
| `metafiles.c` | Metafiles (IBM: `gpimeta.c`, `gpimtof.c`, `mt2*.c`) |
| `device.c` | Dev* |
| `internal.h` | State helpers and constants shared by the units |
