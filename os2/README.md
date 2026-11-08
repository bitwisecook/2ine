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
| `baseos/` | `w45baseos` | Base OS: DOSCALLS, and the queue, session, NLS, message, Vio and Kbd DLLs that forward to it |
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
- `bindings.h`: the guest wire marshalling for portable execution (forwarder
  DLLs have none);
- `generated/<dll>-lx.h`: the export table written by `tools/lxapigen.py`;
- `README.md`: IBM's source location for the DLL and how Warp 4.52 built it.

IBM's source paths come from `@BSS` labels in SYM files, object paths in
shipped binaries and the kernel MAPs. A path marked inferred is not stated by
any artifact. In Warp 4.52, QUECALLS, SESMGR, NLS, MSG, VIOCALLS and KBDCALLS
forward to DOSCALL1; PMWIN, PMSHAPI and PMGRE forward to PMMERGE; PMDRAG
forwards to PMCTLS; MDM and MMIO forward to MMPM. 2ine follows the base OS
part: DOSCALLS implements the queue, session, NLS, message, Vio and Kbd
entries at Warp's DOSCALLS ordinals, and QUECALLS, SESMGR, NLS, MSG, VIOCALLS
and KBDCALLS hold only forwarders to them (`baseos/doscalls/README.md` lists
DOSCALLS' units and the IBM units they follow). The PM and multimedia DLLs
still carry their own implementations.

## Headers

`include/` follows the Toolkit's `h/` directory: each declaration is in the
header, and the INCL_* section, where the Toolkit puts it. 2ine declares every
group unconditionally, so includers need not define INCL_* macros. Includes name
the path from the project root (`"os2/include/bsedos.h"`), which keeps the BSD
socket headers apart from the host's.

| Header | Declares (exporting DLLs) |
|---|---|
| `os2.h` | `os2def.h`, `bse.h`, `bsedos16.h`, `pm.h` |
| `os2def.h` | Base types, handles, POINTL/RECTL, `OS2EXPORT`/`OS2API` and `LX_GUEST_DATA_EXPORT` |
| `bse.h` | `bsedos.h`, `bsesub.h`, `bseerr.h` |
| `bsedos.h` (+ `bsetib.h`, `bsememf.h`, `bsexcpt.h`) | 32-bit Dos* API by INCL_DOS* group (DOSCALLS; forwarded by QUECALLS, SESMGR, NLS, MSG) |
| `bsedos16.h` | USE16 Dos16*/DosSem* entries, as in the DDK (DOSCALLS; forwarded by QUECALLS, SESMGR, NLS, MSG) |
| `bsesub.h` | Kbd* and Vio* (DOSCALLS; forwarded by KBDCALLS, VIOCALLS) |
| `bseerr.h` | Base OS error codes |
| `pm.h` | The PM headers below |
| `pmwin.h` (+ `pmerr.h`, `pmsei.h`) | Win* by INCL_WIN* group (PMWIN) |
| `pmshl.h` | Prf*, switch list, WinStartApp (PMSHAPI) |
| `pmhelp.h` | Help manager (HELPMGR) |
| `pmgpi.h`, `pmdev.h`, `pmfont.h` | Gpi* by INCL_GPI* group, Dev*, font records (PMGPI) |
| `pmstddlg.h` | File and font dialogs, Drg* (PMCTLS, PMDRAG) |
| `pmspl.h` | Spl* (PMSPL) |
| `pmwp.h` | Workplace Shell API and SOM class constructors and data (PMWP) |
| `os2win.h` | Open32 (PMWINX) |
| `os2me.h`, `mmioos2.h` | mci* (MDM), mmio* (MMIO) |
| `dive.h` | Dive* (DIVE) |
| `som/som.h` | SOM |
| `sys/*.h`, `netinet/in.h`, `arpa/inet.h`, `netdb.h`, `resolv.h`, `unistd.h` | 32-bit sockets (TCPIP32) |
| `stack16/sys/socket.h`, `stack16/netdb.h` | 16-bit-stack socket and database sets (SO32DLL, TCP32DLL) |

`include/private/` holds the DDK-style private companions: `bsep.h` (DOSCALLS
entries absent from the Toolkit, launch and session services, queue and
shared-memory helpers), `pmp.h` (PMWIN services for other DLLs, PMGRE and
PMMERGE entries), `pmgpip.h`, `pmdevp.h` and `pmsplp.h` (printer catalogue and
default queue), `os2mep.h` (multimedia), the export framework `export*.h`, and
the guest binding policies `api16.json`/`api32.json` read by `tools/lxapigen.py`.
