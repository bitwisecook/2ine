#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise resource dialogs and stock procedure callbacks with real x86 guests."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def template():
    data = bytearray(128)
    struct.pack_into("<7H", data, 0, len(data), 0, 437, 14, 1, 1, 0)
    struct.pack_into("<6HI4h3H", data, 14, 0, 1, 0, 1, 4, 100, 0, 0, 0, 100, 80, 777, 65535, 65535)
    struct.pack_into(
        "<6HI4h3H", data, 44, 0, 0, 0, 3, 2, 108, 0x80020000, 2, 2, 40, 12, 1, 65535, 65535
    )
    data[100:105] = b"Wire\0"
    data[108:111] = b"OK\0"
    return data


def program(checked):
    g = FlatGuest()
    window = g.field(bytes(4))
    child = g.field(bytes(4))
    previous = g.field(bytes(4))
    initialized = g.field(bytes(4))
    message = g.field(bytes(32))
    data = g.field(template())
    g.code = bytearray(b"\xe9\0\0\0\0")
    callback = 0x10005
    g.code.extend(b"\x55\x89\xe5\x8b\x45\x08\xa3" + struct.pack("<I", window))
    g.code.extend(b"\x83\x7d\x0c\x3b\x0f\x85\0\0\0\0")  # WM_INITDLG
    fallback = len(g.code) - 4
    g.code.extend(b"\x81\x7d\x14\x34\x12\0\0\x75\x0a")
    g.code.extend(b"\xc7\x05" + struct.pack("<II", initialized, 1))
    g.call(2, 729, (window,), 99)
    g.code.extend(b"\xc9\xc3")
    struct.pack_into("<i", g.code, fallback, len(g.code) - fallback - 4)
    for offset in (20, 16, 12, 8):
        g.code.extend(bytes((0xFF, 0x75, offset)))
    g.code.extend(b"\xe8\0\0\0\0")
    g.fixups.append(struct.pack("<BBhBH", 8, 1, len(g.code) - 4, 2, 910))
    g.code.extend(b"\x83\xc4\x10\xc9\xc3")
    struct.pack_into("<i", g.code, 1, len(g.code) - 5)
    g.call(2, 763, 0)
    g.call(2, 716, 1, 0)
    g.call(2, 922, 1, 0, callback, data, 0x1234)
    g.word(initialized, 1, 10)
    g.call_handle(2, 796, window)
    g.result(99, 11)
    g.call_handle(2, 899, window, 1)
    g.code.extend(b"\xa3" + struct.pack("<I", child))
    point = g.field(struct.pack("<ii", 3, 3))
    g.call_handle(2, 883, window, 1)  # WinShowWindow
    g.result(1, 32)
    g.call_handle(2, 787, window, point, 1, 1)  # Dialog units to pixels
    g.result(1, 40)
    g.call_handle(2, 900, window, point, 1)  # WinWindowFromPoint, checked POINTL bridge
    g.require(b"\x3b\x05" + struct.pack("<I", child), 33)
    g.call_handle(2, 900, window, 0xFFFFFFFF, 1)
    g.result(0, 34)
    space = g.field(bytes(4))
    g.call_handle(2, 757, child)  # WinGetPS
    g.code.extend(b"\xa3" + struct.pack("<I", space))
    g.call_handle(3, 592, space, 0, 3, 0, 16, 0xFFFFFFFF)
    g.result(1, 41)  # RGB mode must not dereference the ignored palette.
    g.call_handle(2, 848, space)
    g.result(1, 42)
    g.call_handle(2, 844, child, 0xFFFFFFFF)
    g.result(1, 12)
    g.call_handle(2, 929, child, callback)
    g.code.extend(b"\xa3" + struct.pack("<I", previous))
    # Calling the returned stock procedure must never expose a 64-bit host address.
    for value in (0, 0, 0x124):
        g.code.extend(b"\x68" + struct.pack("<I", value))
    g.code.extend(b"\xff\x35" + struct.pack("<I", child))
    g.code.extend(b"\xff\x15" + struct.pack("<I", previous) + b"\x83\xc4\x10")
    g.result(0, 13)
    g.call_handle(2, 929, child, (previous,))
    g.call_handle(2, 728, window)
    g.result(1, 14)
    g.call(2, 923, 1, 0, callback, 0, 777, 0x1234)
    g.result(99, 15)
    g.call(2, 923, 1, 0, callback, 0, 778, 0x1234)
    g.result(65535, 16)
    if checked:
        g.call(2, 922, 1, 0, callback, 0xFFFFFFFF, 0)
        g.result(0, 17)
        g.call(2, 787, 1, 0xFFFFFFFF, 1, 1)
        g.result(0, 18)
    g.call(2, 902, 1, 0x1001, 0x55, 0)
    g.result(1, 20)
    g.call(2, 915, 1, message, 0, 0x1001, 0x1001)
    g.result(1, 21)
    g.word(message, 0, 22)
    g.word(message + 8, 0x55, 23)
    mle = g.field(bytes(4))
    placeholder = g.field(b"placeholder\0")
    transfer = g.field(b"first\r\nsecond\0")
    offset = g.field(bytes(4))
    output = g.field(bytes(32))
    export_size = g.field(struct.pack("<I", 32))
    g.call(2, 909, 1, 0xFFFF000A, placeholder, 0x10, 0, 0, 100, 80, 0, 4, 42, 0, 0)
    g.code.extend(b"\xa3" + struct.pack("<I", mle))
    g.call_handle(2, 920, mle, 0x1C6, 0, 11)
    g.result(11, 30)
    g.call_handle(2, 920, mle, 0x1C2, transfer, 7)
    g.result(1, 31)
    g.call_handle(2, 920, mle, 0x1C3, offset, 20)
    g.result(7, 32)
    g.word(offset, 7, 33)
    g.call_handle(2, 920, mle, 0x1C2, transfer + 7, 6)
    g.call_handle(2, 920, mle, 0x1C3, offset, 6)
    g.result(6, 34)
    g.word(offset, 13, 35)
    g.call_handle(2, 920, mle, 0x1C2, output, 32)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", offset, 0))
    g.call_handle(2, 920, mle, 0x1C4, offset, export_size)
    g.result(13, 36)
    g.word(export_size, 19, 39)
    g.word(output, int.from_bytes(b"firs", "little"), 37)
    g.word(output + 7, int.from_bytes(b"seco", "little"), 38)
    g.call_handle(2, 728, mle)
    g.call(2, 888, 1)
    g.result(1, 19)
    g.exit(0)
    return linear_image(
        g.code,
        g.fixups,
        ["DOSCALLS", "PMWIN", "PMGPI"],
        "DIALOGS",
        data=g.data,
        resources=[(4, 777, template())],
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-dialogs-") as temporary:
        work = Path(temporary)
        executable = work / "dialogs.exe"
        executable.write_bytes(program(args.backend == "unicorn"))
        env = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            LD_LIBRARY_PATH=str(build),
            SDL_VIDEO_DRIVER="dummy",
            SDL_RENDER_DRIVER="software",
        )
        result = subprocess.run(
            [str(build / "lx_loader"), str(executable)],
            cwd=work,
            env=env,
            capture_output=True,
            timeout=15,
        )
        if result.returncode:
            raise RuntimeError(
                f"dialog exit={result.returncode}\n" + result.stderr.decode(errors="replace")
            )
        print(
            "guest modal callbacks, resource dialogs, stock procedure bridge and checked spans passed"
        )


if __name__ == "__main__":
    main()
