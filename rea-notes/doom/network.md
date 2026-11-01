# Legacy socket modules

OS/2 Doom 1.10 imports SO32DLL and TCP32DLL, not the newer TCPIP32 interface.
The authoritative import records are the IMPDEF COMENT records in
sdk/os2tk45-master/lib/{so32dll,tcp32dll}.lib. SO32DLL imports used by Doom are
2=bind, 8=ioctl, 11=recvfrom, 14=sendto, 16=socket, 20=sock_errno. TCP32DLL imports
are 3=lswap, 4=bswap, 5=inet_addr, 11=gethostbyname, 44=gethostname. New module
exports use these ordinal assignments; existing TCPIP32 behavior is untouched.

SDK authority: h/stack16/sys/socket.h and netinet/in.h use a 16-bit sockaddr
family, port at offset 2, IPv4 address at offset 4, total size 16. The newer
h/sys/socket.h has byte length/family fields and cannot be substituted. Legacy
ioctl takes four arguments and FIONBIO=0x667e/FIONREAD=0x667f. The published
source110/i_net.c uses the four-argument FIONBIO call. h/stack16/nerrno.h defines
SOCBASEERR=10000 and BSD error offsets (EWOULDBLOCK=35). Host errno numbering is
translated explicitly through a platform HAL query.

Implementation in tools/src/2ine-hal adds SO32DLL/TCP32DLL and checked portable
wire bridges. Real sockets, bind, datagram send/receive, nonblocking operation,
FIONREAD, close, hostname and IPv4 lookup are supported. Native resolver structs
borrow libc storage. Portable resolver results serialize fixed-width pointer
arrays, addresses, names and aliases into per-thread guest storage. More than
32 addresses/aliases or a result larger than 4096 bytes returns failure.
Unsupported socket families/types/protocols and ioctls return explicit errors.

Verification, 2026-11-01:

- Native build targets so32dll/tcp32dll pass with the existing Release GCC i386
  configuration in build-sdl3-native.
- tests/os2libs/test_legacy_socket.c compiled with GCC -O2 -m32 -Wall -Wextra
  -Werror, linked to native module libraries, passes actual local UDP delivery,
  source address/port, 16-bit family layout, nonblocking empty read (10035),
  byte counts, invalid family (10047), bad fd (10009), byte swapping, hostname,
  and localhost lookup.
- Portable build targets so32dll/tcp32dll pass with the existing Release Clang
  configuration in build-sdl3-portable. The same test compiled as a 64-bit host
  contract passes. This proves the host module functions; it does not exercise
  guest bridge invocation.
- Extended tests/hal/test_socket.c passes TCP/UDP loopback, poll/select,
  nonblocking empty receive and available-byte count on the portable runtime.
- Real local sockets and native i386 tests need approved execution outside the
  sandbox; sandbox syscall/socket denial is not a product failure.
- clang-format 21.1.8 and source-boundary check pass for these additions.

Native test executable: .state/2ine/doom/test-legacy-socket; portable host test:
.state/2ine/doom/test-legacy-socket-portable. Additional policy entries supplied
in .state/2ine/doom/network-api32.json are merged by the primary agent. Full
Doom two-peer network gameplay remains unverified; module contracts alone do
not prove that integration gate.

A stronger guest check was subsequently compiled with the retained native
Open Watcom 1.9 wcc386/wlink (scratch executable copies; the archived files lack
execute permission). Exact compiler options: -bt=os2 -mf -5r with the matching
c-cpp/h headers. `.state/2ine/doom/network-guest.lnk` links the matching OS/2 C
runtime, OS2386 and the authentic SDK SO32DLL/TCP32DLL import libraries. The
result `.state/2ine/doom/network-guest.exe` passes under **both native and
Unicorn lx_loader**. It dereferences guest resolver structures, names, alias
arrays and IPv4 pointer arrays, performs UDP send/receive through legacy guest
sockaddrs, verifies nonblocking 10035, and closes both sockets normally.
The test binds loopback port 47684; run serially, with that port available.
This verifies actual guest bridge calls beyond the earlier host-only contracts;
it still does not establish a complete two-peer Doom session.

The later source-built multiplayer integration gate is now verified under
both native and Unicorn workers. See network-game.md for the actual source
defects, host loopback identity policy, precise commands, stable runtime
identities and two-peer recorded-tick/render/input/departure evidence. This
supersedes the earlier unverified game integration status above.
