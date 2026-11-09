#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""A USE32 installer wrapper may exit before its detached session installs files."""

import argparse
import os
import select
import signal
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from tests.integration.harness.images import FlatGuest, linear_image


def open_file(g, path, flags, mode):
    name, handle, action = g.field(path.encode() + b"\0"), g.field(bytes(4)), g.field(bytes(4))
    g.call(1, 273, name, handle, action, 0, 0, flags, mode | 0x80, 0)
    g.result(0, 41)
    return handle


def write_file(g, handle, data):
    source, count = g.field(data), g.field(bytes(4))
    g.call(1, 282, (handle,), source, len(data), count)
    g.result(0, 42)
    g.word(count, len(data), 43)


def image(role):
    g = FlatGuest()
    if role == "worker":
        open_file(g, "worker-life", 1, 0x41)
        ready = open_file(g, "ready", 1, 0x41)
        write_file(g, ready, b"R")
        g.call(1, 257, (ready,))
        gate = open_file(g, "gate", 1, 0x40)
        data, count = g.field(bytes(1)), g.field(bytes(4))
        g.call(1, 281, (gate,), data, 1, count)
        g.result(0, 44)
        g.word(count, 1, 45)
        marker = open_file(g, "installed.exe", 0x12, 0x41)
        write_file(g, marker, b"MZinstalled")
        g.call(1, 257, (marker,))
    else:
        target = "middle.exe" if role == "wrapper" else "worker.exe"
        name = g.field(target.encode() + b"\0")
        environment = g.field(b"ONLY_GUEST_ENV=custom\0\0")
        if role == "wrapper":
            open_file(g, "root-life", 1, 0x41)
            data = bytearray(60)
            struct.pack_into("<H", data, 0, 60)
            struct.pack_into("<I", data, 12, name)
            struct.pack_into("<I", data, 24, environment)
            start, session, pid = g.field(data), g.field(bytes(4)), g.field(bytes(4))
            g.call(2, 37, start, session, pid)
        else:
            arguments, result = g.field(target.encode() + b"\0\0"), g.field(bytes(8))
            g.call(1, 283, 0, 0, 4, arguments, environment, result, name)  # EXEC_BACKGROUND
        g.result(0, 46)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "SESMGR"], role.upper(), data=g.data)


def receive(fd):
    assert select.select([fd], [], [], 10)[0], "guest lifetime/ready pipe timed out"
    return os.read(fd, 32)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--backend", required=True, choices=("unicorn", "native"))
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="2ine-installer-lifetime-") as temporary:
        work = Path(temporary)
        for role in ("wrapper", "middle", "worker"):
            (work / (role + ".exe")).write_bytes(image(role))
        channels = {}
        for name in ("root-life", "worker-life", "ready", "gate"):
            os.mkfifo(work / name)
            mode = os.O_RDONLY if name.endswith("life") else os.O_RDWR
            channels[name] = os.open(work / name, mode | os.O_NONBLOCK)
        env = {
            key: value
            for key, value in os.environ.items()
            if not key.startswith("TWOINE_") and key != "IS_2INE"
        }
        env.update(
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            TWOINE_IPC_DIR=str(work / "ipc"),
            LD_LIBRARY_PATH=str(build),
            SDL_VIDEO_DRIVER="dummy",
            SDL_RENDER_DRIVER="software",
        )
        with (work / "loader.log").open("w+b") as log:
            process = subprocess.Popen(
                [str(build / "lx_loader"), "--operation", str(work / "wrapper.exe")],
                cwd=work,
                env=env,
                stdout=log,
                stderr=log,
            )
            try:
                assert receive(channels["ready"]) == b"R"
                assert receive(channels["root-life"]) == b"", "wrapper has not exited"
                assert process.poll() is None, "supervisor exited while installer worker lived"
                assert not (work / "installed.exe").exists()
                os.write(channels["gate"], b"x")
                assert process.wait(timeout=10) == 0
                assert receive(channels["worker-life"]) == b""
                assert (work / "installed.exe").read_bytes() == b"MZinstalled"
            except BaseException:
                log.seek(0)
                print(log.read().decode(errors="replace"))
                raise
            finally:
                if process.poll() is None:
                    process.send_signal(signal.SIGUSR2)
                    process.wait(timeout=10)
                for fd in channels.values():
                    os.close(fd)
    print("Real LX detached session and background grandchild finish before operation completion")


if __name__ == "__main__":
    main()
