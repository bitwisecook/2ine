# Real two-peer OS/2 Doom gameplay

Bounded acceptance gate: two actual source-built OS/2 Doom LX processes must
arbitrate as two nodes, advance at least 175 genuine two-player game ticks,
render and react to each player's keyboard movement, record the same complete
command stream at both peers, and process a normal departure with both process
statuses zero. No synthetic Doom packets, binary patches, system interface
changes, or externally addressed messages are used.

## Inputs and authority

Published os2doom110-src.zip SHA-256
b4de031a2316d529f504d21f2095768aafa88eb789a7c46415f43186e53d15b9;
source110/i_net.c, d_net.c, g_game.c, d_ticcmd.h and i_main.c establish the
socket calls, packet/checksum layout, level startup, demos and shutdown path.
The SDK 4.5 stack16 socket ABI and import authority remain as recorded in
network.md. No decompiler inference is required for these published sources.

The reproducible builder uses the original 68 C units, tmap.asm, retained
Open Watcom 1.9 and SDK imports. Existing clock, renderer and header fixes
remain in research/patches/doom110-clock.patch. Two full clean output trees
network-source-final and network-source-repro produce the same executable:
a31b8d68c6d7b7d8fbff6ccb007f96b3bfd3ece82f926ae8d5e0cfe6038be9cd.
Both build.json receipts declare patched source, toolchain, headers, library
and output hashes; default SOURCE_DATE_EPOCH is zero.

## Reproduced application defects and fixes

1. The initial real UDP handshake completed at both peers: logs reported
   `player 1 of 2 (2 nodes)` and `player 2 of 2 (2 nodes)`. Peer 2 then printed
   `Error: Tried to transmit to another node` and exited. D_DoomMain used
   G_DeferedInitNew, whose later G_DoNewGame clears netgame/playeringame and
   resets consoleplayer/settings. Startup now calls G_InitNew directly after
   arbitration, retaining the negotiated state. Menu new-game behavior is
   unchanged. Raw logs: .state/2ine/doom/network-game-01/peer-*/worker.log.
2. With startup fixed, gameplay stayed black and peer 2 reported repeated
   `bad packet checksum`. This port widened the classic command to four ints,
   followed by two bytes and two padding bytes. Its serializer still converted
   angleturn/consistancy with 16-bit htons/ntohs and built a local record
   field-by-field, leaving the checksum-covered padding uninitialized.
   PacketSend/PacketGet now copy the entire native record, then use 32-bit
   htonl/ntohl for those int fields. This preserves the original 20-byte command
   layout and checksum algorithm while recovering all field bits and padding.
   Raw logs: .state/2ine/doom/network-game-02/peer-*/worker.log.

These are source-level application corrections. The original binaries and
source archive are preserved. No DOSCALLS/socket behavior is changed to hide
either application defect.

## Local peer identities

Doom binds a wildcard receive socket on the same port at every peer and
identifies remote players by IPv4 source address. Its separate send socket
would normally choose the same source address in two local processes.

Optional `TWOINE_SOCKET_BIND_ADDRESS` in hal/posix/host_socket.c restricts a
wildcard bind to the chosen IPv4 address and binds an unbound sendto socket to
that address with an ephemeral port before first send. Explicit application
binds retain their address; an unset/empty policy preserves the normal host
behavior. Invalid IPv4 policy returns EINVAL. The verifier assigns loopback
127.0.0.1 and 127.0.0.2, which need no interface or namespace changes on Linux.
This is scoped host routing policy; guest sockaddr, imports and game packets
remain authentic. The HAL contract test covers real TCP/UDP, nonblocking and
readiness behavior, selected receiver/sender address and invalid policy.

## Verifier

tests/integration/doom_network.py creates separate fresh game directories and
private Xvfb displays. Both workers receive matching `-warp 1 1 -nomonsters
-skill 1 -nosound -nomusic -record network -port PORT -net PLAYER .REMOTE`.
It gives each player genuine XTest W movement, captures the rendered changes,
then sends F10/Y to player 2. Player 1 receives NCMD_EXIT, finalizes its demo
through G_CheckDemoStatus, prints `Demo network.lmp recorded` and exits0.
That observed source behavior is expected when recording multiplayer demos.

Both demos must declare players1/2 active, identify their correct console
player, finish with marker0x80, contain at least175 complete eight-byte
two-player ticks, include movement from both players, and contain exactly
equal command streams after their 13-byte headers. Startup success alone is
insufficient. Worker logs reject faults, missing imports and checksum errors.
The final verifier checks all worker/module hashes before and after the run
and rejects any runtime mutation during verification.

Final native and portable runtime identity runs use the stable parent-owned
build trees under .state/2ine/doom-finish, after the fullscreen rebuild. Earlier
private-runtime and build-sdl3 runs are exploratory; the final receipts below
are the acceptance evidence.

```
env PATH=/usr/bin:/bin python3 tools/src/2ine-hal/tests/integration/doom_network.py --build .state/2ine/doom-finish/build-native --backend native --program .state/2ine/doom/network-source-final/os2doom.exe --wad .state/2ine/doom/playable/doom1.wad --work .state/2ine/doom/network-stable-native
env PATH=/usr/bin:/bin python3 tools/src/2ine-hal/tests/integration/doom_network.py --build .state/2ine/doom-finish/build-portable --backend unicorn --program .state/2ine/doom/network-source-final/os2doom.exe --wad .state/2ine/doom/playable/doom1.wad --work .state/2ine/doom/network-stable-portable
```

Native i386 execution, Xvfb and real loopback sockets were run with approved
execution outside the sandbox. Ruff0.15.20 passes the new harness and builder;
ty0.0.57 passes the builder. The actual clock patch-body concurrency/rollover
test still passes. HAL socket compilation uses GCC with Wall/Wextra/Werror;
its real loopback test passes. Source comments contain contract descriptions,
while experiment history stays here.

This gate covers two local real peers on Linux with the same rebuilt game and
matching shareware WAD. It does not assert interoperation with an unmodified
broken source build, another Doom protocol implementation, external machines,
other WADs, or a four-player session.

## Final results, 2026-11-01

- native: PASS, receipt `.state/2ine/doom/network-stable-native/result.json`. Runtime lib2ine SHA-256
  444e78d4a94933e407885741e2d909eb2cf5bc1c5936c18bbc2fcc1ce74dc1af. Both peers recorded
  289 game ticks; movement tic counts per player
  were [28, 28] at each peer. Complete command
  streams matched, both processes exited0, and every worker/module identity
  remained stable. Rendered view differences were
  99.0%/76.7%.
- portable: PASS, receipt `.state/2ine/doom/network-stable-portable/result.json`. Runtime lib2ine SHA-256
  d0c730a8093c81cc2439fb8bfd1b794e16022f7ecd6f82c5e4b608caca39e3ec. Both peers recorded
  290 game ticks; movement tic counts per player
  were [28, 28] at each peer. Complete command
  streams matched, both processes exited0, and every worker/module identity
  remained stable. Rendered view differences were
  99.0%/76.3%.


Final acceptance repeated both two-peer games with the complete source variant
7604e9aa38e91c862ea07cd25ec4aa7578a912b844c75ad7d62cb9907b532aee
and frozen runtime-full-native/portable workers. Both pairs pass with identical
recorded command streams and exit0. Receipts:
`.state/2ine/doom-finish/verified-network-{native,portable}/result.json`.
These supersede the source-variant identity of the earlier networking-only
passes without changing their packet or socket conclusions.
