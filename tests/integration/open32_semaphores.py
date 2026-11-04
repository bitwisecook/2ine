#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Execute Open32 semaphore counts, limits, timed waits, handles and named IPC."""

import argparse
import os
import select
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def store(g, address):
    g.code.extend(b"\xa3" + struct.pack("<I", address))


def image(g, name):
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], name, data=g.data)


def error(g, expected, status):
    g.call(2, 300)
    g.result(expected, status)


def count_program(checked):
    g = FlatGuest()
    handle, previous = g.field(bytes(4)), g.field(struct.pack("<I", 0xA55A))
    security = g.field(struct.pack("<III", 12, 0, 0))
    for initial, maximum in ((-1, 1), (2, 1), (0, 0)):
        g.call(2, 96, 0, initial & 0xFFFFFFFF, maximum, 0)
        g.result(0, 1)
        error(g, 87, 2)
    g.call(2, 96, security, 1, 2, 0)
    store(g, handle)
    g.call(2, 711, (handle,), 0)
    g.result(0, 3)
    g.call(2, 711, (handle,), 5)
    g.result(258, 4)
    for count, expected in ((0, 87), (0xFFFFFFFF, 87), (3, 298)):
        g.call(2, 562, (handle,), count, previous)
        g.result(0, 5)
        error(g, expected, 6)
        g.word(previous, 0xA55A, 7)
    if checked:
        g.call(2, 562, (handle,), 1, 0xDEADBEEF)
        g.result(0, 8)
        error(g, 87, 9)
        g.call(2, 711, (handle,), 0)
        g.result(258, 10)
        g.call(2, 96, 0xDEADBEEF, 1, 1, 0)
        g.result(0, 11)
        error(g, 87, 12)
    g.call(2, 562, (handle,), 2, previous)
    g.result(1, 13)
    g.word(previous, 0, 14)
    g.call(2, 562, (handle,), 1, previous)
    g.result(0, 15)
    error(g, 298, 16)
    for _ in range(2):
        g.call(2, 711, (handle,), 0)
        g.result(0, 17)
    g.call(2, 711, (handle,), 0)
    g.result(258, 18)
    g.call(2, 44, (handle,))
    g.result(1, 19)
    g.call(2, 711, (handle,), 0)
    g.result(0xFFFFFFFF, 20)
    error(g, 6, 21)
    g.call(2, 562, (handle,), 1, previous)
    g.result(0, 22)
    error(g, 6, 23)
    g.call(2, 44, (handle,))
    g.result(0, 24)
    g.exit(0)
    return image(g, "SEMCOUNT")


def named_program():
    g = FlatGuest()
    name, missing = g.field(b"Open32Semaphore\0"), g.field(b"MissingSemaphore\0")
    first, second, wait_only, release_only, previous = (g.field(bytes(4)) for _ in range(5))
    g.call(2, 507, 0x1F0003, 0, missing)
    g.result(0, 30)
    error(g, 2, 31)
    g.call(2, 96, 0, 1, 2, name)
    store(g, first)
    error(g, 0, 32)
    # Existing objects ignore replacement counts and get distinct handles.
    g.call(2, 96, 0, 0xFFFFFFFF, 0, name)
    store(g, second)
    error(g, 183, 33)
    g.load(first)
    g.require(b"\x3b\x05" + struct.pack("<I", second) + b"\x0f\x95\xc0\x3c\x01", 34)
    g.call(2, 711, (first,), 0)
    g.result(0, 35)
    g.call(2, 711, (second,), 0)
    g.result(258, 36)
    g.call(2, 507, 0x100000, 0, name)
    store(g, wait_only)
    g.call(2, 507, 2, 0, name)
    store(g, release_only)
    g.call(2, 562, (wait_only,), 1, previous)
    g.result(0, 37)
    error(g, 5, 38)
    g.call(2, 711, (release_only,), 0)
    g.result(0xFFFFFFFF, 39)
    error(g, 5, 40)
    for current in (first, second):
        g.call(2, 44, (current,))
        g.result(1, 41)
    g.call(2, 562, (release_only,), 2, previous)
    g.result(1, 42)
    g.word(previous, 0, 43)
    for _ in range(2):
        g.call(2, 711, (wait_only,), 0)
        g.result(0, 44)
    g.call(2, 711, (wait_only,), 0)
    g.result(258, 45)
    for current in (wait_only, release_only):
        g.call(2, 44, (current,))
        g.result(1, 46)
    g.call(2, 507, 0x1F0003, 0, name)
    g.result(0, 47)
    error(g, 2, 48)
    g.call(2, 96, 0, 0, 1, name)
    store(g, first)
    error(g, 0, 49)
    g.call(2, 711, (first,), 0)
    g.result(258, 50)
    g.call(2, 44, (first,))
    g.exit(0)
    return image(g, "SEMNAMED")


def thread_program():
    g = FlatGuest()
    sem, thread, tid, result = (g.field(bytes(4)) for _ in range(4))
    g.call(2, 96, 0, 0, 1, 0)
    store(g, sem)
    g.call(2, 98, 0, 0, 0xAABBCCDD, (sem,), 0, tid)
    store(g, thread)
    g.call(2, 711, (sem,), 0xFFFFFFFF)
    g.result(0, 60)
    g.call(2, 711, (thread,), 3000)
    g.result(0, 61)
    g.call(2, 1006, (thread,), result)
    g.result(1, 62)
    g.word(result, 42, 63)
    for current in (thread, sem):
        g.call(2, 44, (current,))
        g.result(1, 64)
    g.exit(0)
    callback = 0x10000 + len(g.code)
    g.call(2, 673, 20)
    g.call(2, 562, (sem,), 1, 0)
    g.result(1, 65)
    g.code.extend(b"\xb8\x2a\0\0\0\xc3")
    g.code = g.code.replace(struct.pack("<I", 0xAABBCCDD), struct.pack("<I", callback))
    return image(g, "SEMTHREAD")


def cancelled_program():
    g = FlatGuest()
    name = g.field(b"CancelledSemaphoreWait\0")
    sem, tid, entered = (g.field(bytes(4)) for _ in range(3))
    g.call(2, 96, 0, 0, 1, name)
    store(g, sem)
    g.call(1, 311, tid, 0xAABBCCDD, 0, 0, 65536)
    g.result(0, 80)
    loop = len(g.code)
    g.call(1, 229, 1)
    g.load(entered)
    g.code.extend(b"\x85\xc0\x0f\x84" + struct.pack("<i", loop - len(g.code) - 8))
    g.call(1, 229, 50)
    g.call(1, 111, (tid,))
    g.result(0, 81)
    g.call(1, 349, tid, 0)
    g.result(0, 82)
    g.call(2, 44, (sem,))
    g.result(1, 83)
    g.call(2, 507, 0x1F0003, 0, name)
    g.result(0, 84)
    error(g, 2, 85)
    g.exit(0)
    worker = 0x10000 + len(g.code)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", entered, 1))
    g.call(2, 711, (sem,), 0xFFFFFFFF)
    g.exit(86)
    g.code = g.code.replace(struct.pack("<I", 0xAABBCCDD), struct.pack("<I", worker))
    return image(g, "SEMCANCEL")


def ipc_program(owner):
    g = FlatGuest()
    name, handle = g.field(b"CrossProcessSemaphore\0"), g.field(bytes(4))
    if owner:
        g.call(2, 96, 0, 0, 1, name)
    else:
        g.call(2, 507, 0x1F0003, 0, name)
    store(g, handle)
    if owner:
        text, actual = g.field(b"ready\n"), g.field(bytes(4))
        g.call(1, 282, 1, text, 6, actual)
        g.result(0, 70)
        g.call(2, 711, (handle,), 5000)
        g.result(0, 71)
    else:
        g.call(2, 562, (handle,), 1, 0)
        g.result(1, 72)
    g.call(2, 44, (handle,))
    g.result(1, 73)
    g.exit(0)
    return image(g, "SEMOWNER" if owner else "SEMPEER")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-semaphores-") as directory:
        work = Path(directory)
        env = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            LD_LIBRARY_PATH=str(build),
            TWOINE_IPC_DIR=str(work / "ipc"),
            SDL_VIDEO_DRIVER="dummy",
            SDL_AUDIO_DRIVER="dummy",
        )
        env.pop("TWOINE_BALL", None)

        def command(payload, name):
            path = work / (name + ".exe")
            path.write_bytes(payload)
            return [str(build / "lx_loader"), str(path)]

        def run(payload, name):
            result = subprocess.run(
                command(payload, name), cwd=work, env=env, capture_output=True, timeout=15
            )
            assert result.returncode == 0, (
                f"{name}: exit={result.returncode}\n" + result.stderr.decode(errors="replace")
            )

        run(count_program(args.backend == "unicorn"), "count")
        run(named_program(), "named")
        run(thread_program(), "thread")
        if args.backend == "unicorn":
            run(cancelled_program(), "cancelled")
        owner = subprocess.Popen(
            command(ipc_program(True), "owner"),
            cwd=work,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            ready, _, _ = select.select([owner.stdout], [], [], 10)
            assert ready and owner.stdout.readline() == b"ready\n", "owner did not signal readiness"
            run(ipc_program(False), "peer")
            out, err = owner.communicate(timeout=10)
            assert owner.returncode == 0, f"owner exit={owner.returncode}\n{out!r}\n" + err.decode(
                errors="replace"
            )
        finally:
            if owner.poll() is None:
                owner.kill()
                owner.communicate()
        print(
            "Open32 semaphore counts, overflow, timed waits, stale handles, worker wakeups and named IPC passed"
        )


if __name__ == "__main__":
    main()
