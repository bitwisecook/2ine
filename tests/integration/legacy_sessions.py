#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Run a USE16 parent with far STARTDATA and an LX termination-queue child."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image
from legacy import Guest


def parent():
    g = Guest()
    queue = g.field(struct.pack("<HH", 0, 0xA55A))
    sid = g.field(struct.pack("<HH", 0, 0x5AA5))
    pid = g.field(struct.pack("<HH", 0, 0xA55A))
    request, pointer = g.field(bytes(4)), g.field(bytes(4))
    length = g.field(struct.pack("<HH", 0, 0x5AA5))
    priority = g.field(bytes(1))
    name = g.field(b"\\QUEUES\\LegacySession\0")
    child = g.field(b"child.exe\0")
    environment = g.field(b"SESSION_FIXTURE=ready\0\0")
    data = bytearray(60)
    struct.pack_into("<HH", data, 0, 60, 1)
    struct.pack_into("<H", data, 28, 1)
    for at, offset in ((12, child), (20, name), (24, environment)):
        struct.pack_into("<H", data, at, offset)
    start = g.field(data)
    for at in (12, 20, 24):
        g.code.extend(b"\x8c\xd8\xa3" + struct.pack("<H", start + at + 2))
    g.pointer(queue)
    g.push(0)
    g.pointer(name)
    g.call(1, 681)
    g.zero(1)
    g.word(queue + 2, 0xA55A, 2)
    g.pointer(start)
    g.pointer(sid)
    g.pointer(pid)
    g.call(1, 702)
    g.zero(3)
    g.word(sid + 2, 0x5AA5, 4)
    g.word(pid + 2, 0xA55A, 5)
    status = g.field(struct.pack("<HHH", 6, 2, 1))
    g.code.extend(b"\xff\x36" + struct.pack("<H", sid))
    g.pointer(status)
    g.call(1, 699)
    g.zero(6)
    g.code.extend(b"\xff\x36" + struct.pack("<H", queue))
    g.pointer(request)
    g.pointer(length)
    g.pointer(pointer)
    g.push(0)
    g.push(0)
    g.pointer(priority)
    g.push(0)
    g.push(0)
    g.call(1, 674)
    g.zero(7)
    g.word(length, 4, 8)
    g.word(length + 2, 0x5AA5, 9)
    # ES:BX receives the far address of the copied termination record.
    g.code.extend(b"\xc4\x1e" + struct.pack("<H", pointer))
    g.code.extend(b"\x26\x8b\x07")
    g.require(b"\x3b\x06" + struct.pack("<H", sid), 10)
    g.require(b"\x26\x81\x7f\x02\x2f\x00", 11)
    g.code.extend(b"\xff\x36" + struct.pack("<H", pointer + 2))
    g.call(1, 39)
    g.zero(12)
    g.code.extend(b"\xff\x36" + struct.pack("<H", queue))
    g.call(1, 675)
    g.zero(13)
    g.exit(0)
    return g.image(False)


def child():
    g = FlatGuest()
    variable = g.field(b"SESSION_FIXTURE\0")
    value = g.field(bytes(4))
    g.call(1, 227, variable, value)
    g.result(0, 40)
    g.load(value)
    g.require(b"\x81\x38read", 41)
    g.require(b"\x66\x81\x78\x04y\0", 42)
    g.call(1, 229, 250)
    g.exit(47)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "SESSCHLD", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-session16-") as temporary:
        work = Path(temporary)
        path = work / "parent.exe"
        path.write_bytes(parent())
        (work / "child.exe").write_bytes(child())
        env = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            TWOINE_IPC_DIR=str(work / "ipc"),
            LD_LIBRARY_PATH=str(build),
            SDL_VIDEO_DRIVER="dummy",
            SDL_RENDER_DRIVER="software",
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
                f"legacy session exit={result.returncode}\n"
                + result.stderr.decode(errors="replace")
            )
    print("USE16 STARTDATA environment, session state and termination queue passed")


if __name__ == "__main__":
    main()
