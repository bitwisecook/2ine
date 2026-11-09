<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# TCPIP32.DLL

32-bit BSD sockets, resolver and network utilities.

- IBM source: Unknown.
- Evidence: `debug/symbols/mptn/dll/tcpip32.sym` (330 symbols).
- In Warp 4.52: Separate TCP/IP product; only the SYM files are on the client
  media, so the entry table (ordinals, forwarders) cannot be checked. Nothing
  in the SYMs suggests forwarding between TCPIP32, SO32DLL and TCP32DLL.
- Exports: `tcpip32.def`.

## Units

No IBM file names are known; the units are named by function.

| 2ine unit | APIs and services |
|---|---|
| `module.c` | Entry: the generated export table, the `_ress` data export, upper-case export aliases |
| `sockets.c` | socket, bind, connect, send/recv, select, ioctl, sysctl, socket options, byte order, writev, sock_errno |
| `resolver.c` | inet_addr/inet_ntoa/inet_network, gethostby*, getprotoby*, getservby*, gethostname, gethostid, res_init and `_res`, tcp_h_errno |
| `internal.h` | The socket error and resolver state the units share |

Shared with SO32DLL and TCP32DLL (`os2/mptn/common/`): socket structure
layouts (`network.c`), socket errors (`socket_errors.c`) and host lookups
(`resolver.c`).
