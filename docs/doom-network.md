<!-- Public domain. No rights reserved. Wholly AI-authored. -->

# Doom network games

Use the [source-built executable](doom-build.md) on every peer, with matching
game versions and IWADs. Doom uses UDP port 5029 by default; `-port NUMBER`
selects another port and must match on all peers. Literal IPv4 addresses in
the `-net` list begin with a dot.

For two machines, launch player 1 with the other machine's address:

```sh
lx_loader os2doom.exe -warp 1 1 -net 1 .192.168.1.22
```

Launch player 2 with player 1's address:

```sh
lx_loader os2doom.exe -warp 1 1 -net 2 .192.168.1.21
```

The host must allow inbound UDP on the chosen port. Doom waits for every
configured peer before starting. F10 followed by Y leaves the game normally.

For two peers on the same Linux host, use separate game directories and set
`TWOINE_SOCKET_BIND_ADDRESS=127.0.0.1` for player 1 and
`TWOINE_SOCKET_BIND_ADDRESS=127.0.0.2` for player 2. Give each peer the other's
address in `-net`. This optional host policy replaces wildcard IPv4 binds and
selects the source address for unbound `sendto` sockets. Explicit application
bind addresses retain their values. With the variable unset, sockets use the
normal host behavior. No network interface changes are needed for these
loopback addresses.

The Linux integration verifier starts private Xvfb displays and separate game
directories, records both players' actual commands, and verifies remote input,
rendered movement, network departure and process exits:

```sh
python3 tests/integration/doom_network.py --build /path/2ine/build \
  --backend native --program /path/doom-build/os2doom.exe \
  --wad /path/DOOM1.WAD --work /path/network-results
```

Use `--backend unicorn` with a portable build. The verifier disables audio and
uses the selected worker's socket modules. It writes screenshots, worker logs,
two genuine Doom demos and `result.json`. During recording, Doom ends the
remaining peer's demo and exits when it receives the other player's departure.
