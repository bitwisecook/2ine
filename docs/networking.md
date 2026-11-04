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
network ioctls are unsupported. The underlying socket transport currently uses
IPv4 TCP/UDP; this is not an NDIS driver, raw Ethernet bridge or IPX stack.

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
