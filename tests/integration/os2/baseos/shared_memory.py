#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise address-based anonymous shared-memory attachment and peer grants."""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from tests.integration.harness.images import FlatGuest, linear_image

ADDRESS = 0x30000000


def parent(gettable):
    g = FlatGuest()
    shared, private = g.field(bytes(4)), g.field(bytes(4))
    result, waited = g.field(bytes(8)), g.field(bytes(8))
    pid = g.field(bytes(4))
    filename = g.field(b"child.exe\0")
    command = g.field(b"child.exe\0\0")
    g.call(1, 299, private, 4096, 19)
    g.result(0, 1)
    g.call_handle(1, 303, private, 0, 3)
    g.result(303, 2)
    g.call_handle(1, 302, private, 3)
    g.result(487, 3)
    flags = 19 | (0x100 if gettable else 0x200)
    g.call(1, 300, shared, 0, 4096, flags)
    g.result(0, 4)
    g.word(shared, ADDRESS, 5)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", ADDRESS, 0x11223344))
    g.call(1, 283, 0, 0, 2, command, 0, result, filename)
    g.result(0, 6)
    if not gettable:
        g.call_handle(1, 303, private, (result,), 3)
        g.result(487, 13)
        g.call_handle(1, 303, shared, (result,), 4)
        g.result(5, 14)
        g.call_handle(1, 303, shared, (result,), 3)
        g.result(0, 7)
    g.call(1, 280, 0, 0, waited, pid, (result,))
    g.result(0, 8)
    g.word(waited + 4, 0, 9)
    g.word(ADDRESS + 4, 0x55667788, 10)
    g.call_handle(1, 304, shared)
    g.result(0, 11)
    g.call_handle(1, 304, private)
    g.result(0, 12)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "SHMPAR", data=g.data)


def child():
    g = FlatGuest()
    attempts = g.field(struct.pack("<I", 2000))
    g.call(1, 302, ADDRESS + 1, 3)
    g.result(487, 20)
    loop = len(g.code)
    g.call(1, 302, ADDRESS, 1)
    g.code.extend(b"\x85\xc0\x0f\x84" + bytes(4))
    attached = len(g.code) - 4
    g.result(5, 21)
    g.call(1, 229, 1)
    g.code.extend(b"\xff\x0d" + struct.pack("<I", attempts) + b"\x0f\x85")
    g.code.extend(struct.pack("<i", loop - len(g.code) - 4))
    g.exit(22)
    struct.pack_into("<i", g.code, attached, len(g.code) - attached - 4)
    g.word(ADDRESS, 0x11223344, 23)
    g.call(1, 302, ADDRESS, 8)
    g.result(87, 27)
    g.call(1, 302, ADDRESS, 4)
    g.result(5, 28)
    g.call(1, 302, ADDRESS, 3)
    g.result(0, 29)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", ADDRESS + 4, 0x55667788))
    g.call(1, 304, ADDRESS)
    g.result(0, 24)
    g.call(1, 302, ADDRESS, 3)
    g.result(0, 25)
    g.word(ADDRESS + 4, 0x55667788, 26)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "SHMCHLD", data=g.data)


def peer_receiver(pool=False):
    g = FlatGuest()
    attempts = g.field(struct.pack("<I", 5000))
    loop = len(g.code)
    g.call(1, 302, ADDRESS, 3)
    g.code.extend(b"\x85\xc0\x0f\x84" + bytes(4))
    attached = len(g.code) - 4
    g.call(1, 229, 1)
    g.code.extend(b"\xff\x0d" + struct.pack("<I", attempts) + b"\x0f\x85")
    g.code.extend(struct.pack("<i", loop - len(g.code) - 4))
    g.exit(40)
    struct.pack_into("<i", g.code, attached, len(g.code) - attached - 4)
    if pool:
        allocation = g.field(bytes(4))
        g.call(1, 344, ADDRESS, 4, 4096)
        g.result(0, 50)
        g.call(1, 345, ADDRESS, allocation, 8)
        g.result(0, 51)
        g.call(1, 346, ADDRESS, (allocation,), 8)
        g.result(0, 52)
        g.call(1, 347, ADDRESS)
        g.result(0, 53)
        g.call(1, 345, ADDRESS, allocation, 8)
        g.result(532, 54)
        g.call(1, 344, ADDRESS, 4, 4096)
        g.result(0, 55)
        g.call(1, 345, ADDRESS, allocation, 8)
        g.result(0, 56)
        g.call(1, 346, ADDRESS, (allocation,), 8)
        g.result(0, 57)
        g.call(1, 347, ADDRESS)
        g.result(0, 58)
    data = ADDRESS + (64 if pool else 0)
    g.word(data, 0x11223344, 41)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", data + 4, 0x55667788))
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "PEERREAD", data=g.data)


def peer_sender(receiver, other_scope, pool=False):
    g = FlatGuest()
    shared = g.field(bytes(4))
    attempts = g.field(struct.pack("<I", 5000))
    g.call(1, 300, shared, 0, 4096, 19 | 0x200)
    g.result(0, 42)
    g.word(shared, ADDRESS, 43)
    if pool:
        allocation = g.field(bytes(4))
        g.call(1, 344, ADDRESS, 5, 4096)
        g.result(0, 60)
        g.call(1, 345, ADDRESS, allocation, 16)
        g.result(0, 61)
        g.word(allocation, ADDRESS + 64, 62)
    data = ADDRESS + (64 if pool else 0)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", data, 0x11223344))
    g.call_handle(1, 303, shared, other_scope, 3)
    g.result(303, 44)
    loop = len(g.code)
    g.call_handle(1, 303, shared, receiver, 3)
    g.code.extend(b"\x85\xc0\x0f\x84" + bytes(4))
    granted = len(g.code) - 4
    g.call(1, 229, 1)
    g.code.extend(b"\xff\x0d" + struct.pack("<I", attempts) + b"\x0f\x85")
    g.code.extend(struct.pack("<i", loop - len(g.code) - 4))
    g.exit(45)
    struct.pack_into("<i", g.code, granted, len(g.code) - granted - 4)
    loop = len(g.code)
    g.code.extend(b"\x81\x3d" + struct.pack("<II", data + 4, 0x55667788) + b"\x0f\x84" + bytes(4))
    finished = len(g.code) - 4
    g.call(1, 229, 1)
    g.code.extend(b"\xff\x0d" + struct.pack("<I", attempts) + b"\x0f\x85")
    g.code.extend(struct.pack("<i", loop - len(g.code) - 4))
    g.exit(46)
    struct.pack_into("<i", g.code, finished, len(g.code) - finished - 4)
    if pool:
        g.call(1, 345, ADDRESS, allocation, 8)
        g.result(0, 63)
        g.word(allocation, ADDRESS + 80, 64)
        g.call(1, 346, ADDRESS, (allocation,), 8)
        g.result(0, 65)
        g.call(1, 347, ADDRESS)
        g.result(0, 66)
        g.call(1, 345, ADDRESS, allocation, 8)
        g.result(532, 67)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "PEERWRITE", data=g.data)


def independent_peers(build, backend, pool=False):
    with tempfile.TemporaryDirectory(prefix="twoine-peer-shared-") as temporary:
        work = Path(temporary)
        receiver = work / "receiver.exe"
        receiver.write_bytes(peer_receiver(pool))
        idle = FlatGuest()
        idle.call(1, 229, 10000)
        idle.exit(0)
        other = work / "other.exe"
        other.write_bytes(
            linear_image(idle.code, idle.fixups, ["DOSCALLS"], "OTHERPEER", data=idle.data)
        )
        env = dict(
            os.environ,
            TWOINE_BACKEND=backend,
            TWOINE_LIBPATH=str(build),
            TWOINE_IPC_DIR=str(work / "ipc"),
            TWOINE_BALL="peer-fixture",
        )
        processes = []
        try:
            alien = subprocess.Popen(
                [str(build / "lx_loader"), str(other)],
                cwd=work,
                env={**env, "TWOINE_BALL": "different-ball"},
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            processes.append(alien)
            deadline = time.monotonic() + 5
            while not list((work / "ipc").glob(f"2ine-peer-*-{alien.pid:08x}.data")):
                if alien.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("The different-ball peer did not register its process lease")
                time.sleep(0.005)
            reader = subprocess.Popen(
                [str(build / "lx_loader"), str(receiver)],
                cwd=work,
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            processes.append(reader)
            sender = work / "sender.exe"
            sender.write_bytes(peer_sender(reader.pid, alien.pid, pool))
            result = subprocess.run(
                [str(build / "lx_loader"), str(sender)],
                cwd=work,
                env=env,
                capture_output=True,
                timeout=15,
            )
            if result.returncode:
                raise RuntimeError(
                    f"independent sender exit={result.returncode}\n"
                    + result.stderr.decode(errors="replace")
                )
            _, error = reader.communicate(timeout=10)
            if reader.returncode:
                raise RuntimeError(
                    f"independent receiver exit={reader.returncode}\n"
                    + error.decode(errors="replace")
                )
        finally:
            for process in processes:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    for gettable in (False, True):
        with tempfile.TemporaryDirectory(prefix="twoine-shared-") as temporary:
            work = Path(temporary)
            path = work / "parent.exe"
            path.write_bytes(parent(gettable))
            (work / "child.exe").write_bytes(child())
            env = dict(
                os.environ,
                TWOINE_BACKEND=args.backend,
                TWOINE_LIBPATH=str(build),
                TWOINE_IPC_DIR=str(work / "ipc"),
                LD_LIBRARY_PATH=str(build),
            )
            result = subprocess.run(
                [str(build / "lx_loader"), str(path)],
                cwd=work,
                env=env,
                capture_output=True,
                timeout=15,
            )
            if result.returncode:
                raise RuntimeError(
                    f"shared memory gettable={gettable} exit={result.returncode}\n"
                    + result.stderr.decode(errors="replace")
                )
    independent_peers(build, args.backend)
    independent_peers(build, args.backend, pool=True)
    print(
        "Anonymous grants, independent scoped peers, address attachment and mapping revival passed"
    )


if __name__ == "__main__":
    main()
