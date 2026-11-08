<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# OS/2 DLLs

`os2/` holds 2ine's OS/2 DLL implementations. Its groups follow the component
build trees IBM used for OS/2 Warp 4.5x, as named in the shipped SYM files,
debug data and kernel MAPs. 2ine builds one shared library per DLL
(`libpmwin.so` provides PMWIN).

| Directory | IBM build tree | Product area |
|---|---|---|
| `include/` | `rel\os2c\include\base\os2` | Public API headers, named like the OS/2 Toolkit's `h/` |
| `include/private/` | DDK private companions (`bsep.h`, `pmp.h`, …) | Cross-DLL internals, the export framework (`export*.h`) and the guest binding policies (`api16.json`, `api32.json`) |
| `baseos/` | `w45baseos` | Base OS: DOSCALLS and the queue, session, NLS, message, Vio and Kbd DLLs |
| `ps/` | `w45ps\src\ps` | Presentation Manager: PMWIN, PMGPI, PMGRE, PMSHAPI, PMMERGE, the controls (`controls/`), HELPMGR and Open32 (`awe/` = PMWINX) |
| `ps/common/` | `ps\common` | Text helpers shared by PM DLLs |
| `wpsh/` | `w45wpsh` | Workplace Shell (PMWP) and the spooler (PMSPL) |
| `som/` | `build\src\somk` | SOM runtime |
| `mm/` | `w4mm\src\mm` | Multimedia: MDM (`mci/mdm`), MMIO (`mmio/mmio`), DIVE (`video/dive`); `midi/` and `video/decode/` are 2ine's synthesizer and video decoder |
| `mptn/` | MPTN (`\MPTN\DLL`) | TCP/IP: TCPIP32, SO32DLL, TCP32DLL; `common/` holds their socket layouts |
| `opengl/` | `src/gos/3d` | OpenGL (OPENGL.DLL, including the `pgl*` entries) |
| `thirdparty/` | not IBM | SSLEAY32 |

Each DLL directory holds:

- `<dll>.def`: the LINK386 module definition (`LIBRARY`, and `EXPORTS`
  entries `name [= C entry] @ordinal`; `name = MODULE.ordinal` or
  `name = MODULE.entry` declares a forwarder);
- `module.c`: the DLL entry and the only includer of the export table;
- `bindings.h`: the guest wire marshalling for portable execution;
- `generated/<dll>-lx.h`: the export table written by `tools/lxapigen.py`;
- `README.md`: IBM's source location for the DLL and how Warp 4.52 built it.

IBM's source paths come from `@BSS` labels in SYM files, object paths in
shipped binaries and the kernel MAPs. A path marked inferred is not stated by
any artifact. In Warp 4.52, QUECALLS, SESMGR, NLS, MSG, VIOCALLS and KBDCALLS
forward to DOSCALL1; PMWIN, PMSHAPI and PMGRE forward to PMMERGE; PMDRAG
forwards to PMCTLS; MDM and MMIO forward to MMPM. 2ine builds each of these
with its own implementation.
