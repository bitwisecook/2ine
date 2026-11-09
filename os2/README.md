<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# OS/2 DLLs

`os2/` holds 2ine's OS/2 DLL implementations. Its groups follow the component
build trees IBM used for OS/2 Warp 4.5x, as named in the shipped SYM files,
debug data and kernel MAPs. 2ine builds one shared library per DLL
(`libpmgpi.so` provides PMGPI).

| Directory | IBM build tree | Product area |
|---|---|---|
| `include/` | `rel\os2c\include\base\os2` | Public API headers, named like the OS/2 Toolkit's `h/` |
| `include/private/` | DDK private companions (`bsep.h`, `pmp.h`, …) | Cross-DLL internals, the export framework (`export*.h`) and the guest binding policies (`api16.json`, `api32.json`) |
| `baseos/` | `w45baseos` | Base OS: DOSCALLS, and the queue, session, NLS, message, Vio and Kbd DLLs that forward to it |
| `ps/` | `w45ps\src\ps` | Presentation Manager: PMMERGE (`pmmerge/`, which implements PMWIN, PMGRE and PMSHAPI), the PMWIN, PMGRE and PMSHAPI forwarders, PMGPI, the controls (`controls/`: PMCTLS and the PMDRAG forwarder), HELPMGR and Open32 (`awe/` = PMWINX) |
| `ps/common/` | `ps\common` | Text helpers shared by PM DLLs |
| `wpsh/` | `w45wpsh` | Workplace Shell (PMWP) and the spooler (PMSPL) |
| `som/` | `build\src\somk` | SOM runtime |
| `registry/` | `w45registry\src` | The OS/2 registry: REGISTRY (`reg32/`), which implements the Reg* entries PMWINX forwards to |
| `mm/` | `w4mm\src\mm` | Multimedia (MMPM/2): MMPM (`mmpm/`, which implements MDM and MMIO), the MDM (`mci/mdm`) and MMIO (`mmio/mmio`) forwarders, DIVE (`video/dive`); `midi/` and `video/decode/` are 2ine's synthesizer, MIDI file parser and video decoder |
| `mptn/` | MPTN (`\MPTN\DLL`) | TCP/IP: TCPIP32, SO32DLL, TCP32DLL; `common/` holds their socket layouts, socket errors and host lookups |
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
forwards to PMCTLS; MDM and MMIO forward to MMPM; PMWINX forwards its Reg*
entries to REGISTRY. 2ine follows all of these: DOSCALLS implements the queue, session, NLS, message, Vio and
Kbd entries at Warp's DOSCALLS ordinals, and QUECALLS, SESMGR, NLS, MSG,
VIOCALLS and KBDCALLS hold only forwarders to them (`baseos/doscalls/README.md`
lists DOSCALLS' units and the IBM units they follow). PMMERGE implements the
PMWIN, PMGRE and PMSHAPI entries at Warp's PMMERGE ordinals, PMCTLS the Drg*
entries at Warp's PMCTLS ordinals, and PMWIN, PMGRE, PMSHAPI and PMDRAG hold
only forwarders (`ps/pmmerge/README.md` and `ps/controls/pmctls/README.md`
list the units). MMPM implements the MDM and MMIO entries at Warp's MMPM
ordinals and MDM and MMIO hold only forwarders (`mm/mmpm/README.md` lists the
units); REGISTRY implements the Reg* entries and PMWINX forwards them at the
same ordinals. A native DLL that calls another DLL's C entry points links the
implementing library (`libpmmerge.so`, `libpmctls.so`, `libmmpm.so`,
`libregistry.so`), not the forwarder.

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
| `pmwin.h` (+ `pmerr.h`, `pmsei.h`) | Win* by INCL_WIN* group (PMMERGE; forwarded by PMWIN) |
| `pmshl.h` | Prf*, switch list, WinStartApp (PMMERGE; forwarded by PMSHAPI) |
| `pmhelp.h` | Help manager (HELPMGR) |
| `pmgpi.h`, `pmdev.h`, `pmfont.h` | Gpi* by INCL_GPI* group, Dev*, font records (PMGPI) |
| `pmstddlg.h` | File and font dialogs, Drg* (PMCTLS; Drg* forwarded by PMDRAG) |
| `pmspl.h` | Spl* (PMSPL) |
| `pmwp.h` | Workplace Shell API and SOM class constructors and data (PMWP) |
| `os2win.h` | Open32 (PMWINX) |
| `os2me.h` (+ `os2medef.h`, `meerror.h`, `mcios2.h`, `mmioos2.h`) | MCI messages, flags and errors, mci* and mmio* (MMPM; forwarded by MDM, MMIO) |
| `dive.h` | Dive* (DIVE) |
| `som/som.h` | SOM |
| `sys/*.h`, `netinet/in.h`, `arpa/inet.h`, `netdb.h`, `nerrno.h`, `resolv.h`, `unistd.h` | 32-bit sockets and socket errors (TCPIP32) |
| `stack16/sys/socket.h`, `stack16/netdb.h` | 16-bit-stack socket and database sets (SO32DLL, TCP32DLL) |

`include/private/` holds the DDK-style private companions: `bsep.h` (DOSCALLS
entries absent from the Toolkit, launch and session services, queue and
shared-memory helpers), `pmp.h` (PMMERGE services for other DLLs, the PMGRE
and PMMERGE-only entries, PMCTLS's standard-dialog hooks, the video-frame
service), `pmgpip.h`, `pmdevp.h` and `pmsplp.h` (printer catalogue and
default queue), `os2mep.h` (MMIO services for the MCI devices and the guest
layout of the MCI parameter blocks), the export framework `export*.h`, and
the guest binding policies `api16.json`/`api32.json` read by `tools/lxapigen.py`.
