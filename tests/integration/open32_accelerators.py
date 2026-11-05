#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Check Open32 ACCEL APIs against real LX resources and synchronous callbacks."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image
from open32_contracts import call, store


def program():
    g = FlatGuest()
    loaded, created, window, command, kind, delivered = (g.field(bytes(4)) for _ in range(6))
    output = g.field(bytes([0xA5]) * 32)
    message = g.field(bytes(28))
    bad = g.field(bytes([0x80]) + bytes(5))
    array = g.field(
        b"\x01\xee"
        + struct.pack("<HH", 0x70, 0x22)
        + b"\x00\xff"
        + struct.pack("<HH", ord("a"), 0x33)
        + b"\x10\x00"
        + struct.pack("<HH", ord("b"), 0x99)
    )
    g.call(2, 457, 0, 91)
    store(g, loaded)
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 1)
    g.call(2, 1026, (loaded,), 0, 0)
    g.result(3, 2)
    g.call(2, 1026, (loaded,), output + 1, 1)
    g.result(1, 3)
    g.require(b"\x80\x3d" + struct.pack("<I", output) + b"\xa5", 4)
    g.require(b"\x80\x3d" + struct.pack("<I", output + 7) + b"\xa5", 5)
    g.word(output + 1, 0x00710001, 6)  # F2, including canonical zero padding.
    g.call(2, 457, 0, 92)
    g.result(0, 7)
    call(g, "GetLastError")
    g.result(13, 8)
    g.call(2, 57, bad, 1)
    g.result(0, 9)
    for count in (0, 32768, 0xFFFFFFFF):
        g.call(2, 57, array, count)
        g.result(0, 10)
    g.call(2, 57, 0xFFFFFFF0, 3)
    g.result(0, 11)
    g.call(2, 57, array, 3)
    store(g, created)
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 12)
    g.call(2, 1026, (created,), output, 3)
    g.result(3, 13)
    g.word(output, 0x00700001, 14)
    name = g.field(b"AcceleratorContract\0")
    record = g.field(struct.pack("<10I", 0, 0x88776655, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, name))
    call(g, "RegisterClass", record)
    call(g, "CreateWindowEx", 0, name, name, 0, 0, 0, 100, 50, 0, 0, 0xFFFFFFFF, 0)
    store(g, window)
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 15)

    def translate(table, msg, key, parameter=0, expected=1):
        for offset, value in ((4, msg), (8, key), (12, parameter)):
            g.code.extend(b"\xc7\x05" + struct.pack("<II", message + offset, value))
        g.call(2, 697, (window,), (table,), message)
        g.result(expected, 16)
        g.word(message + 4, msg, 17)  # Translate does not rewrite the caller's MSG.
        g.word(message + 8, key, 18)

    translate(created, 0x100, 0x70)
    g.word(command, 0x10022, 19)
    g.word(delivered, 1, 20)
    translate(created, 0x102, ord("a"))
    g.word(command, 0x10033, 21)
    translate(created, 0x101, 0x70, expected=0)
    translate(created, 0x100, 0x71, expected=0)
    translate(loaded, 0x100, 0x71)
    g.word(command, 0x10041, 22)
    # Native PM input updates real Open32 Ctrl state before translation.
    g.call(3, 920, (window,), 0x7A, 0x18010015, ord("o"))
    translate(loaded, 0x100, ord("O"))
    g.word(command, 0x10042, 23)
    g.call(3, 920, (window,), 0x7A, 0x18010045, ord("o"))
    translate(loaded, 0x100, 0x72)
    g.word(command, 0x1F060, 24)
    g.word(kind, 0x112, 25)
    call(g, "EnableWindow", (window,), 0)
    translate(created, 0x100, 0x70, expected=0)
    call(g, "EnableWindow", (window,), 1)
    # The command callback destroys this table. No outstanding table lease may block it.
    translate(created, 0x106, ord("b"), 1 << 29)
    g.word(command, 0x10099, 26)
    g.call(2, 1026, (created,), 0, 0)
    g.result(0, 27)
    g.call(2, 141, (created,))
    g.result(0, 28)
    g.call(2, 141, (loaded,))
    g.result(1, 29)
    call(g, "DestroyWindow", (window,))
    call(g, "UnregisterClass", name, 0xFFFFFFFF)
    g.exit(0)
    procedure = 0x10000 + len(g.code)
    g.code.extend(b"\x8b\x44\x24\x08\x3d\x11\x01\x00\x00\x0f\x84" + bytes(4))
    command_jump = len(g.code) - 4
    g.code.extend(b"\x3d\x12\x01\x00\x00\x0f\x85" + bytes(4))
    return_jump = len(g.code) - 4
    struct.pack_into("<i", g.code, command_jump, len(g.code) - command_jump - 4)
    store(g, kind)
    g.code.extend(b"\x8b\x44\x24\x0c")
    store(g, command)
    g.code.extend(b"\xff\x05" + struct.pack("<I", delivered))
    g.code.extend(b"\x3d\x99\x00\x01\x00\x0f\x85" + bytes(4))
    destroy_jump = len(g.code) - 4
    g.call(2, 141, (created,))
    g.result(1, 30)
    for jump in (return_jump, destroy_jump):
        struct.pack_into("<i", g.code, jump, len(g.code) - jump - 4)
    g.code.extend(b"\xb8\x01\x00\x00\x00\xc3")
    g.data = g.data.replace(struct.pack("<I", 0x88776655), struct.pack("<I", procedure))
    resource = struct.pack(
        "<HH9H", 3, 437, 2, 0x21, 0x41, 0x13, ord("o"), 0x42, 0x102, 0x22, 0xF060
    )
    malformed = struct.pack("<HH3H", 2, 437, 2, 0x20, 1)
    return linear_image(
        g.code,
        g.fixups,
        ["DOSCALLS", "PMWINX", "PMWIN"],
        "ACCEL32",
        data=g.data,
        resources=[(8, 91, resource), (8, 92, malformed)],
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-open32-accelerators-") as temporary:
        work = Path(temporary)
        guest = work / "accelerators.exe"
        guest.write_bytes(program())
        env = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            LD_LIBRARY_PATH=str(build),
            SDL_VIDEO_DRIVER="dummy",
            SDL_RENDER_DRIVER="software",
        )
        result = subprocess.run(
            [str(build / "lx_loader"), str(guest)],
            cwd=work,
            env=env,
            capture_output=True,
            timeout=20,
        )
        if result.returncode:
            raise RuntimeError(
                f"Open32 accelerators exit={result.returncode}\n"
                + result.stderr.decode(errors="replace")
            )
    print(
        "Open32 accelerator LX resources, checked copies, modifiers and synchronous delivery passed"
    )


if __name__ == "__main__":
    main()
