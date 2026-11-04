# Host networking and TLS

OS/2 TCP and UDP sockets use host sockets in the host network namespace. The
host chooses the source IP, route, DNS resolver and link-layer MAC. There is no
second machine identity, DHCP lease or private NAT subnet. Existing host port
bindings apply to guest listeners too. `TWOINE_SOCKET_BIND_ADDRESS` can restrict
wildcard binds and unbound TCP/UDP senders to a particular host IPv4 address.

`hal/network.h` provides fresh, owned snapshots of IPv4 interfaces and routes,
interface MAC addresses, masks, MTUs, broadcast/point-to-point addresses, DNS
server addresses and search domains. macOS reads interfaces with `getifaddrs`,
routes with routing `sysctl`, and global DNS with SystemConfiguration. Linux
reads interfaces with `getifaddrs`, unicast routes with rtnetlink, and resolver
configuration from `/etc/resolv.conf`. DNS server strings preserve IPv6
addresses. Host name resolution continues to use the host resolver, including
its split DNS/VPN policies; this does not depend on guest RESOLV or RESOLV2 files.
Snapshots are not atomic across those system services. The host remains
authoritative for actual routing, including policy and multipath routes.

The SO32DLL bridge exposes read-only interface address, flags, mask, broadcast,
destination, metric and MTU queries, `SIOCGIFCONF`, `SIOSTATIF`/`SIOSTATIF42`,
`SIOSTATRT` and `SIOSTATCNTRT`. TCPIP32 exposes the corresponding BSD-layout
`ioctl` and `os2_ioctl` queries. Nested guest pointers and buffer extents are
translated explicitly. Interface MIB rows contain the actual host MAC; unknown
statistics remain zero and host kernel pointers are never returned. Mutating
network ioctls are unsupported. TCPIP32 also implements the OS/2 routing
`sysctl` queries `NET_RT_DUMP`, `NET_RT_FLAGS` and `NET_RT_IFLIST`, with packed
OS/2 route/interface messages. This supports the original `netstat -r`,
`netstat -n` and interface queries such as `ifconfig lo0` on macOS (`lo` on
Linux). Protocol counters and connection/PCB inventories are not provided;
unavailable interface statistics remain zero. The transport uses IPv4 host
sockets; this is not an NDIS driver, raw Ethernet bridge or IPX stack.

## DNS and resolver configuration

TCPIP32 and legacy TCP32DLL provide forward/reverse IPv4 host lookup, protocol
and service lookup, and per-thread resolver errors and result storage. Guest
pointer arrays and strings are serialized into guest memory. TCPIP32 exposes
the Toolkit `_ress` resolver data and `__res_init`; its socket exports support
the original IBM `DDNSRES.DLL` used by `nslookup.exe`. The original resolver
library handles DNS message construction, record parsing, search lists,
retries and UDP-to-TCP fallback.

The loader supplies `ETC=C:\MPTN\ETC` when no `ETC` is set. On launch it
creates/refreshes `RESOLV2`, `RESOLV` and `HOSTS` there. Resolver files contain
up to three host IPv4 DNS servers and six search domains with CRLF line endings;
`HOSTS` comes from the host's hosts file. Only files beginning with the 2ine
managed-file header are replaced. Existing manual configurations, including
case variants of those filenames, are preserved. Setting `ETC` selects a
different guest directory. Extensionless filenames with a trailing dot, as
opened by the original resolver, resolve correctly.

Host API lookups retain host resolver policies, including split DNS. Programs
which send DNS packets themselves, such as `nslookup`, use the servers in
their resolver files or an explicitly selected server. The global DNS snapshot
does not represent every scoped VPN resolver. Direct DNS requires a reachable
IPv4 DNS server; IPv6-only DNS servers are retained in the host inventory but
cannot be used by the guest IPv4 wire transport. The bridge does not synthesize
DNS answers or hide server timeouts.

## Ping, traceroute and diagnostic sockets

The unchanged OS/2 `ping.exe` and `tracerte.exe` use the TCPIP32 raw socket
bridge. `SOCK_RAW/IPPROTO_ICMP` supplies IPv4-framed ICMP echo replies and,
where the host permits it, ICMP errors. Received IP length/fragment fields use
the OS/2 BSD convention. Both connected `send`/`recv` and `sendto`/`recvfrom`
are supported. IP TTL/TOS options, nonblocking operation and both OS/2 select
APIs are translated to host behavior.

Raw UDP (`IPPROTO_UDP` or UDP packets through `IPPROTO_RAW` with `IP_HDRINCL`)
is sent through host datagram sockets. Probe source/destination ports, payload,
TTL and TOS are preserved; the host constructs IP/UDP framing and checksums.
Real ICMP time-exceeded/port-unreachable packets return through the ICMP
socket, allowing the original traceroute to match its probes. Raw UDP receive,
fragmented packets, IP header options and other raw protocols are unsupported
and fail explicitly. This is diagnostic packet translation, not arbitrary
packet injection.

macOS uses unprivileged ICMP datagram sockets, including for traceroute errors;
no privileged helper is installed. Linux first tries a raw ICMP socket, which
requires `CAP_NET_RAW` or equivalent privileges. Its unprivileged ping-socket
fallback supports echo if permitted by `ping_group_range`; receiving traceroute
ICMP errors on Linux requires the raw-socket capability.

## TLS upgrade setting

**Upgrade Legacy TLS defaults to on**, including for existing balls with no
explicit preference. Change it in Settings → General → Network Security in
either desktop front end, or save it through the CLI:

```sh
python3 tools/ball.py --ball /path/to/Example.2ineball config system.tls_upgrade true
python3 tools/ball.py --ball /path/to/Example.2ineball config system.tls_upgrade false
```

The resulting runtime configuration is `system.tls_upgrade=true` or `false`.
Changes apply on the next application launch. A context captures its policy
when created.

With upgrading enabled, SSLEAY32 imports (including explicit DLL paths) select
the host shim ahead of a guest SSLEAY32.DLL. Legacy SSLv2, SSLv3, TLS 1.0, TLS 1.1
and TLS 1.2 method selectors negotiate TLS 1.2 or newer, normally TLS 1.3. The
version query reports the actual negotiated protocol.

With upgrading disabled, the loader retains its normal guest-DLL search order.
If it falls back to the host shim, version-flexible OpenSSL 1.0 calls and TLS 1.2
selectors use TLS 1.2 only. Older pinned protocols fail context creation rather
than being silently upgraded. Turning off upgrading does not enable obsolete
cryptography in the host library. An original guest TLS DLL retains its own
behavior and certificate policy.

## Host TLS implementation

Builds require **OpenSSL 4.1 or newer**; this change was built and tested with
OpenSSL 4.1.0. CMake discovers the installed library, allowing subsequent patch
releases without changing the shim. Set `OPENSSL_ROOT_DIR` when multiple versions
are installed. A native i386 worker needs matching i386 development libraries.

The HAL supports client/server contexts, TLS 1.2/1.3, PEM trust/certificates,
verified DNS/IP identities, SNI, encrypted reads/writes, pending bytes, clean
shutdown and nonblocking WANT_READ/WANT_WRITE results. Sockets remain owned by
the caller; sessions retain their contexts. Its socket BIO prevents SIGPIPE
without changing process signal handlers.

The first guest adapter is a **client subset of the OpenSSL 1.0 SSLEAY32 ABI**,
using [upstream export ordinals](https://github.com/openssl/openssl/blob/OpenSSL_1_0_2-stable/util/ssleay.num).
It preserves entry points and uses typed, generation-checked 32-bit handles for
host contexts and sessions. It covers initialization, protocol methods, context
creation/trust/cipher selection, session creation, socket attachment, SNI,
connect/handshake, read/write, pending, shutdown, error, verification result and
version queries. Chain and peer-name verification stay enabled in the host
shim, even when an old caller requests `SSL_VERIFY_NONE`. Host OpenSSL's trust
paths and `SSL_CERT_FILE`/`SSL_CERT_DIR` supply default trust; explicit guest CA
paths use the existing drive mapper. The expected peer name is supplied through
the legacy SNI control; clients that omit it fail rather than skip identity
verification. [OpenSSL documents name validation separately from SNI](https://docs.openssl.org/4.1/man3/SSL_set1_host/).

This is not blanket binary compatibility with every OS/2 TLS package. IBM
SSL/GSKit, NSS, other OpenSSL DLL names/ordinal sets, LIBEAY32 crypto and X509
objects, arbitrary BIOs, verification callbacks, server-side guest APIs, session
serialization and direct access to old public SSL structures need their own
adapters. Non-null guest verification callbacks make connection setup fail;
guest callback addresses are never passed to OpenSSL. Unsupported imports fail
resolution. Statically linked TLS cannot be upgraded by replacing a DLL, and
packet rewriting alone cannot change an authenticated TLS handshake.

## Verification

`host_network` compares the interface inventory to host `getifaddrs` and checks
OS/2 address/MAC/routing layouts and short buffers. `host_socket` exercises TCP,
UDP, readiness, source binding and broken-stream writes. `host_tls` and
`os2_tls_shim` use temporary certificates and ephemeral loopback ports to check
encrypted transfer, nonblocking retries, certificate/name rejection, retained
contexts, stale handles and upgrade on/off protocol behavior. `network_guest`
runs generated LX applications through Unicorn to exercise nested ioctl
pointers, encrypted TLS 1.3 calls and guest-versus-host provider selection.
`host_diagnostics` checks echo framing/checksums/identifiers, raw UDP payload and
port translation, real port-unreachable packets and malformed-packet rejection.
`network_diagnostics_guest` verifies both resolver ABIs, stable guest error
pointers, descriptor filtering and managed/manual resolver files.

To test unchanged private IBM tools as well, configure CMake with
`-DTWOINE_OS2_NETWORK_TOOLS=/path/to/MPTN`, containing `bin/{ping,tracerte,host,nslookup,netstat,ifconfig}.exe`
and `dll/ddnsres.dll`. The optional `os2_network_tools` test copies them into a
temporary guest drive and checks ping, traceroute, host lookup, route/interface
queries, DNS A/PTR/MX records, UDP/TCP, truncation fallback, NXDOMAIN and timeout
responses against an isolated loopback DNS fixture. Original binaries are not
distributed with the tests. Linux diagnostic capability requirements apply to
this test too.
