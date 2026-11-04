# OS/2 DNS and diagnostic-tool compatibility

This is a 2ine compatibility-layer change, not a reconstruction milestone gate.
The bounded acceptance gate is unchanged LX IBM network tools using host IPv4
sockets, host resolver configuration, ICMP echo/UDP traceroute, and read-only
route/interface queries. Private original binaries remain outside tracked source.

## Inputs and authority

Original fixture root: `build-lotus-exports-current/Probe.2ineball/drive_c/mptn`.
Selected binaries are x86 LX files; the earlier NE install-media ping is a
different artifact and was not used for these results.

| Original relative path | SHA-256 |
| --- | --- |
| `bin/ping.exe` | `dc5d0946168417e6a8cdbcede21a2207a856b3ec8fcfe4cbe74626274faa1252` |
| `bin/tracerte.exe` | `8e12555a020cbefc67b9fa92b146f44f9b4218cc49844e894e0c6111f033d760` |
| `bin/host.exe` | `df1a7018fc7551cc15d778fcdfe36adf2f159cc3177a91fe57ce62c1c453d637` |
| `bin/nslookup.exe` | `d6d72f44cb4e662c8d28a03974a05ef9b367327c375f61f9ca3967efa2d2d641` |
| `bin/netstat.exe` | `75e0274730bca815def72f628f8c6ac308097d165cc071079e06d24bb79419d7` |
| `bin/ifconfig.exe` | `a754633b588660cc1bd81f563cb34c2c0d6069c64aa99b16ed2fff8ff8f4b263` |
| `dll/ddnsres.dll` | `c4d0c1793726190c6399b5ecab2e8b260341102a6628af827b3e4f3880cff9e9` |

ABI authority: `sdk/os2tk45-master/lib/{tcpip32,tcp32dll}.lib` OMF IMPDEF records
in the parent OS/2 workspace, plus Toolkit `h/{types.h,netdb.h,resolv.h,nerrno.h,
sys/socket.h,sys/sysctl.h,net/if.h,net/if_dl.h,net/route.h,netinet/in.h}`.
Toolkit examples `samples/tcpiptk/socket/{selects,udpc}.c` confirm socket-init
success, address setup, select/FD_ISSET and datagram calling patterns.
Names/ordinals are taken from import libraries, not inferred from decompilation.

Local patched `scripts/rea` with Ghidra 12.2 inspected the SHA-bound ping input.
Evidence ID: `ev_29bf52eee38aed116e9452e0311740902eef42813492b6e3e6fce85c81be9ca8`.
Retained local report: `build-diagnostics-current/evidence/ping-rea.json`.
`lx_dump` imports for ping, tracerte, nslookup and DDNSRES are retained in that
build directory. REA used ephemeral immutable-source analysis; no inferred
source boundaries or persistent original-code annotations are claimed.

## ABI details and observed compatibility issues

- TCPIP32 imports use explicit flat 32-bit stack marshalling. Guest sockaddr is
  16 bytes with length/family bytes; TCP32DLL/SO32DLL legacy addresses retain
  the 16-bit family. Guest pointers never escape into host libc structures.
- `_ress` ordinal 143 is data: two packed 512-byte Toolkit resolver records,
  with search strings/pointers in the same guest allocation. Exporting data
  through an executable-entry bridge fails loader execute checks; it is an
  absolute guest-address export instead.
- DDNSRES imports uppercase by-name `INET_NTOA`, `GETHOSTNAME`, `INET_NETWORK`
  as well as TCPIP32 ordinals. Case aliases now select the same real exports.
- Original DDNSRES opens `resolv2.` / `resolv.`. Missing trailing-dot
  normalization previously hid correctly generated extensionless files.
- OS/2 fd_set is count16, padding16, fd32[64] (260 bytes). Select now filters
  output sets; os2_select edits its descriptor array and uses milliseconds.
- Resolver host/service/protocol data is copied under a libc database lock
  into per-thread snapshots, then serialized into guest memory. Stable guest
  h_errno pointers are refreshed on lookup, including errors. Socket errors
  are captured before intervening host calls.
- Packed sysctl route header is 112 bytes; interface header is 132 bytes,
  including 118-byte if_data; address header is 18 bytes. Output sockaddrs
  have explicit widths, without host pointers or host-ABI structure copies.
- macOS non-root ICMP datagram sockets receive IPv4 framing and diagnostic
  errors. Raw UDP traceroute packets translate to real host UDP probes with
  original ports/payload/TTL/TOS. Connected ICMP send must use send(), since
  Darwin rejects sendto() on that connected socket with EISCONN.

## Verification checkpoint

macOS arm64, optimized Unicorn portable loader, Release build, OpenSSL 4.1.0.

```
cmake -S . -B build-image-app-runtime \
  -DTWOINE_OS2_NETWORK_TOOLS=<private fixture root>
cmake --build build-image-app-runtime --target tcpip32 tcp32dll so32dll lx_loader \
  twoine_socket_tests twoine_diagnostic_tests twoine_network_tests \
  twoine_tls_tests twoine_ssl_shim_tests twoine_legacy_socket_tests -j 8
ctest --test-dir build-image-app-runtime --output-on-failure \
  -R '^(source_boundaries|host_socket|host_network|host_diagnostics|host_tls|os2_tls_shim|legacy_socket|network_guest|network_diagnostics_guest|os2_network_tools)$'
python3 tools/lxapigen.py --check
git diff --check
```

Result: **10/10 tests passed**, 7.93 seconds; generator and whitespace checks
passed. Logs: `build-diagnostics-current/{build,ctest,network-tools-test}.log`.
Original ping: 2 transmitted/2 received, zero loss on loopback. Original
tracerte reaches loopback; raw HAL checks validate actual ICMP port-unreachable
packets and quoted UDP ports. Original host resolves localhost and reverses
127.0.0.1. Original netstat -rn and ifconfig lo0 return actual host inventory.
Original nslookup with original DDNSRES passes A, PTR, MX, explicit TCP,
UDP truncation-to-TCP fallback, NXDOMAIN and timeout fixtures.
The generated LX tests verify both resolver ABIs, stable h_errno pointers,
resolver-data exports, sysctl size queries, invalid guest pointers, select
filtering, default ETC, manual-file preservation and managed-file refresh.

Additional live smoke checks: host.exe resolved example.com through the host
resolver; tracerte.exe with max-TTL 2 received a real first-hop response.
Direct external DNS queries timed out with original nslookup and also native
macOS dig / a native UDP query against the configured global server. This is
recorded as a host upstream failure, not a passing external wire-DNS check.
The isolated wire-DNS tests pass without Internet dependence.

## Limits and next action

No claim of arbitrary raw injection, raw Ethernet/NDIS, fragmented/raw-option
UDP support, IPv6 guest transport, full BIND export compatibility, kernel PCB
inventories, writable network configuration, or host protocol statistics.
Direct wire DNS needs an IPv4 server and does not reproduce every scoped VPN
resolver; high-level host lookups continue using the host resolver.
Linux raw ICMP errors require CAP_NET_RAW; ping-socket echo fallback is
implemented but Linux execution was not verified here (local Docker daemon
unavailable). This leaves Linux runtime validation unverified, not waived.

Next action after this checkpoint: freeze only the networking changes in a
clean release source tree, rerun the network suite there and on the packaged
runtime, push the commit and install the validated local application update.
