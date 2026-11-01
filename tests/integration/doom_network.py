#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Verify two real Doom peers using separate process-local loopback identities."""

import argparse
import json
import os
import select
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

from doom import CONFIG, Game, difference, digest
from linux.x11 import Display, check_errors


def demo_result(path, player):
    data = path.read_bytes()
    assert data[8] == player and data[9:13] == bytes((1, 1, 0, 0)), data[:13]
    assert data[-1] == 0x80 and (len(data) - 14) % 8 == 0
    commands = data[13:-1]
    ticks = len(commands) // 8
    assert ticks >= 175, f"only {ticks} two-player game ticks recorded"
    moving = [sum(commands[i] != 0 for i in range(p * 4, len(commands), 8)) for p in range(2)]
    assert min(moving) > 5, f"missing local or remote movement: {moving}"
    return {"path": str(path), "sha256": digest(path), "ticks": ticks, "moving_tics": moving}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), default="native")
    parser.add_argument("--program", type=Path, required=True)
    parser.add_argument("--wad", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.build, args.program, args.wad, args.work = (
        p.resolve() for p in (args.build, args.program, args.wad, args.work)
    )
    args.work.mkdir(parents=True, exist_ok=True)
    receipt = args.work / "result.json"
    receipt.unlink(missing_ok=True)
    runtime_files = [args.build / "lx_loader", *sorted(args.build.glob("lib*.so"))]
    identities = {p.name: digest(p) for p in runtime_files if p.is_file()}
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    peers = []
    streams = []
    try:
        for index in range(2):
            work = Path(tempfile.mkdtemp(prefix=f"peer-{index + 1}-", dir=args.work))
            game_dir = work / "game"
            game_dir.mkdir()
            shutil.copyfile(args.program, game_dir / "os2doom.exe")
            shutil.copyfile(args.wad, game_dir / "DOOM1.WAD")
            (game_dir / "default.cfg").write_text(CONFIG.replace("use_mouse 1", "use_mouse 0"))
            log = (work / "worker.log").open("w")
            server_log = (work / "xvfb.log").open("w")
            streams.extend((log, server_log))
            read_fd, write_fd = os.pipe()
            server = subprocess.Popen(
                [
                    "Xvfb",
                    "-displayfd",
                    str(write_fd),
                    "-screen",
                    "0",
                    "640x480x24",
                    "-nolisten",
                    "tcp",
                ],
                pass_fds=(write_fd,),
                stdout=server_log,
                stderr=server_log,
            )
            os.close(write_fd)
            peer = {"server": server, "work": work, "game_dir": game_dir, "log": log}
            peers.append(peer)
            try:
                assert select.select([read_fd], [], [], 10)[0], "Xvfb did not start"
                name = ":" + os.read(read_fd, 32).decode().strip()
            finally:
                os.close(read_fd)
            peer["display"] = Display(name)
            environment = {
                **os.environ,
                "DISPLAY": name,
                "SDL_VIDEO_DRIVER": "x11",
                "SDL_RENDER_DRIVER": "software",
                "SDL_AUDIO_DRIVER": "dummy",
                "TWOINE_BACKEND": args.backend,
                "TWOINE_LIBPATH": str(args.build),
                "LD_LIBRARY_PATH": str(args.build),
                "TWOINE_SOCKET_BIND_ADDRESS": f"127.0.0.{index + 1}",
            }
            command = [
                str(args.build / "lx_loader"),
                "os2doom.exe",
                "-warp",
                "1",
                "1",
                "-nomonsters",
                "-skill",
                "1",
                "-nosound",
                "-nomusic",
                "-record",
                "network",
                "-port",
                str(port),
                "-net",
                str(index + 1),
                f".127.0.0.{2 - index}",
            ]
            peer["process"] = subprocess.Popen(
                command, cwd=game_dir, env=environment, stdout=log, stderr=log
            )
        deadline = time.monotonic() + 35
        for peer in peers:
            process, display = peer["process"], peer["display"]
            while time.monotonic() < deadline:
                assert process.poll() is None, f"peer exited: {peer['work']}"
                windows = [
                    (w, title)
                    for w, title in display.windows()
                    if "DOOM" in title.upper()
                    and display.geometry(w)[0] >= 320
                    and display.geometry(w)[1] >= 200
                ]
                if windows:
                    handle = windows[0][0]
                    peer["game"] = Game(process, display, handle, peer["work"], peer["game_dir"])
                    break
                time.sleep(0.05)
            assert "game" in peer, f"no Doom window: {peer['work']}"
        for peer in peers:
            peer["game"].pause(1)
            peer["display"].focus(peer["game"].handle)
            peer["before"] = peer["game"].capture("initial")
        for peer in peers:
            game = peer["game"]
            game.display.focus(game.handle)
            game.held(ord("w"), 0.8)
            after = game.capture("moved")
            peer["movement_difference"] = difference(peer["before"], after)
            assert peer["movement_difference"] > 0.03, "peer input did not move rendered view"
        peers[0]["game"].pause(5)
        game = peers[1]["game"]
        game.display.focus(game.handle)
        game.key(0xFFC7)
        game.capture("quit-confirmation")
        game.display.key(ord("y"))
        assert peers[1]["process"].wait(timeout=12) == 0, "peer did not quit normally"
        # Doom finalizes a multiplayer demo when a remote player leaves, then
        # uses its normal error-message UI path to quit with status zero.
        assert peers[0]["process"].wait(timeout=12) == 0, "remote departure did not end recording"
        results = []
        for index, peer in enumerate(peers):
            peer["log"].flush()
            log = (peer["work"] / "worker.log").read_text(errors="replace")
            assert f"player {index + 1} of 2 (2 nodes)" in log, "network arbitration did not finish"
            if index == 0:
                assert "Demo network.lmp recorded" in log, "remote exit packet was not processed"
            assert not any(
                s in log
                for s in (
                    "SIGSEGV",
                    "CPU stop",
                    "missing import",
                    "Segmentation fault",
                    "bad packet checksum",
                    "consistency failure",
                )
            )
            results.append(
                {
                    "player": index + 1,
                    "work": str(peer["work"]),
                    "movement_difference": peer["movement_difference"],
                    "demo": demo_result(peer["game_dir"] / "network.lmp", index),
                }
            )
        check_errors()
        streams_data = [(peer["game_dir"] / "network.lmp").read_bytes()[13:-1] for peer in peers]
        assert streams_data[0] == streams_data[1], "peers recorded different command streams"
        assert identities == {p.name: digest(p) for p in runtime_files if p.is_file()}, (
            "runtime changed during network verification"
        )
        receipt.write_text(
            json.dumps(
                {
                    "backend": args.backend,
                    "port": port,
                    "program_sha256": digest(args.program),
                    "runtime_sha256": digest(args.build / "lib2ine.so"),
                    "runtime_files": identities,
                    "identical_command_streams": True,
                    "peers": results,
                },
                indent=2,
            )
            + "\n"
        )
        print(receipt)
    finally:
        for peer in peers:
            for name in ("process", "server"):
                process = peer.get(name)
                if process and process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
        for stream in streams:
            stream.close()


if __name__ == "__main__":
    main()
