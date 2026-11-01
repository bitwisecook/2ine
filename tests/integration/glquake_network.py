#!/usr/bin/env python3
# Public domain. No rights reserved.
"""Run two native OS/2 GLQuake peers and verify real UDP game traffic."""

import argparse
import hashlib
import json
import os
import shutil
import socket
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path

from linux.x11 import Display, t, x
from quake_demo import player_positions


@dataclass
class Peer:
    process: subprocess.Popen
    log: Path
    home: Path
    window: int = 0


def wait_log(peer: Peer, fragment: str, seconds=180):
    text = ""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        text = peer.log.read_text(errors="replace")
        if fragment in text:
            return text
        if peer.process.poll() is not None:
            raise RuntimeError(text)
        time.sleep(0.1)
    raise RuntimeError(f"Timeout waiting for {fragment}: {text[-2000:]}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "program", "data", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), default="native")
    args = parser.parse_args()
    build, program, work = args.build.resolve(), args.program.resolve(), args.work.resolve()
    program_sha256 = hashlib.sha256(program.read_bytes()).hexdigest()
    work.mkdir(parents=True)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    display = Display(os.environ["DISPLAY"])
    peers = []
    streams = []
    try:
        for index, name in enumerate(("lanhost", "lanclient")):
            home = work / name
            (home / "id1").mkdir(parents=True, exist_ok=True)
            shutil.copyfile(args.data, home / "id1/pak0.pak")
            (home / "id1/autoexec.cfg").write_text(
                f"name {name}\nhostname lanserver\n"
                f'bind F2 status\nbind F3 "say {name}_ping"\n'
                "bind F4 screenshot\nbind F5 slist\nbind F10 quit\n"
            )
            env = dict(
                os.environ,
                TWOINE_BACKEND=args.backend,
                TWOINE_LIBPATH=str(build),
                LD_LIBRARY_PATH=str(build),
                SDL_VIDEO_DRIVER="x11",
                SDL_RENDER_DRIVER="software",
                SDL_AUDIO_DRIVER="dummy",
                LIBGL_ALWAYS_SOFTWARE="1",
            )
            command = [
                str(build / "lx_loader"),
                str(program),
                "-width",
                "320",
                "-height",
                "240",
                "-nomouse",
                "-inputlog",
                "-nosound",
                "-broadcast",
                "127.255.255.255",
                "-port",
                str(port),
                "+developer",
                "1",
            ]
            if index == 0:
                command += ["-listen", "2", "+nomonsters", "1", "+record", "host", "e1m1"]
            else:
                command += ["+record", "client", "+connect", f"127.0.0.1:{port}"]
            previous = {w for w, _ in display.windows()}
            log = home / "runtime.log"
            stream = log.open("wb")
            streams.append(stream)
            process = subprocess.Popen(
                command, cwd=home, env=env, stdout=stream, stderr=subprocess.STDOUT
            )
            peer = Peer(process, log, home)
            peers.append(peer)
            wait_log(peer, "CL_SignonReply: 4")
            peer.window = next(
                w
                for w, title in display.windows()
                if w not in previous and title == "GLQuake for OS/2"
            )
            x.XMoveWindow(display.d, peer.window, 30 + index * 400, 80)
            x.XFlush(display.d)
        display.focus(peers[1].window)
        display.key(0xFFC2)
        time.sleep(2)
        wait_log(peers[1], "lanserver       ")
        for peer in peers:
            display.focus(peer.window)
            display.key(0xFFBF)
            time.sleep(0.3)
            display.key(0xFFC0)
            time.sleep(0.4)
            display.capture(peer.window, peer.home / "before.png")
        for peer in peers:
            display.focus(peer.window)
            code = x.XKeysymToKeycode(display.d, 0xFF52)
            t.XTestFakeKeyEvent(display.d, code, 1, 0)
            x.XFlush(display.d)
            time.sleep(10 if args.backend == "unicorn" else 1.5)
            t.XTestFakeKeyEvent(display.d, code, 0, 0)
            x.XFlush(display.d)
            time.sleep(0.3)
            display.key(0xFFC1)
            time.sleep(0.4)
            display.capture(peer.window, peer.home / "moved.png")
        time.sleep(0.5)
        for peer in peers:
            wait_log(peer, "lanhost_ping")
            wait_log(peer, "lanclient_ping")
            text = wait_log(peer, "players: 2 active (2 max)")
            assert "lanhost" in text and "lanclient" in text
        for peer in reversed(peers):
            display.focus(peer.window)
            display.key(0xFFC7)
            time.sleep(0.4)
            display.key(ord("y"))
            assert peer.process.wait(timeout=20) == 0
        results = []
        for peer, name in zip(peers, ("host", "client"), strict=True):
            text = peer.log.read_text(errors="replace")
            assert "GLQuake shutdown complete" in text
            demo = peer.home / f"id1/{name}.dem"
            packets, positions = player_positions(demo)
            entity = 1 if name == "host" else 2
            assert packets > 50 and len(positions[entity]) > 10
            span = [
                max(point[axis] for point in positions[entity])
                - min(point[axis] for point in positions[entity])
                for axis in range(2)
            ]
            assert max(span) > 32, (name, span)
            assert (peer.home / "id1/quake00.tga").stat().st_size == 230418
            results.append(
                {
                    "peer": name,
                    "demo_packets": packets,
                    "demo_bytes": demo.stat().st_size,
                    "player_positions": len(positions[entity]),
                    "xy_span": span,
                    "exit": 0,
                }
            )
        assert hashlib.sha256(program.read_bytes()).hexdigest() == program_sha256, (
            "Executable changed during test"
        )
        (work / "results.json").write_text(
            json.dumps(
                {
                    "port": port,
                    "backend": args.backend,
                    "program_sha256": program_sha256,
                    "peers": results,
                },
                indent=2,
            )
            + "\n"
        )
        print("GLQuake real UDP peers, chat, movement, screenshots and clean exit verified")
    finally:
        for peer in peers:
            if peer.process.poll() is None:
                peer.process.kill()
                peer.process.wait()
        for stream in streams:
            stream.close()


if __name__ == "__main__":
    main()
