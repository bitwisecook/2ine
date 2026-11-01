#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Verify guest CREATESTRUCT callbacks, measured fonts and PM window attributes."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def program(checked):
    g = FlatGuest()
    status = g.field(bytes(4))
    bitmap = g.field(bytes(4))
    window = g.field(bytes(4))
    space = g.field(bytes(4))
    count = g.field(bytes(4))
    enumeration = g.field(bytes(4))
    metrics = g.field(bytes(456))
    output = g.field(bytes(64))
    control = g.field(b"\x06\0data")
    class_name = g.field(b"Wire\0")
    title = g.field(b"Wire fixture\0")
    helv = g.field(b"Helv\0")
    font_name = g.field(b"9.Helv\0")
    text = g.field(b"Wide\0")
    callback = bytearray(b"\x83\x7c\x24\x08\x01\x0f\x85\0\0\0\0")
    success_branch = len(callback) - 4
    failures = []

    def require(comparison, condition=b"\x0f\x85"):
        callback.extend(comparison + condition + bytes(4))
        failures.append(len(callback) - 4)

    callback.extend(b"\x8b\x54\x24\x10")  # mp2 = checked, guest-visible CREATESTRUCT
    require(b"\x85\xd2", b"\x0f\x84")
    for offset, expected in (
        (0, 0),
        (4, control),
        (8, 1234),
        (20, 20),
        (24, 30),
        (28, 5),
        (32, 4),
        (48, 1),
    ):
        require(b"\x81\x7a" + bytes([offset]) + struct.pack("<I", expected))
    callback.extend(b"\x8b\x4a\x2c")
    require(b"\x81\x39Wire")
    require(b"\x81\x7c\x24\x0c" + struct.pack("<I", control))
    callback.extend(b"\xc7\x05" + struct.pack("<II", status, 1))
    success = len(callback)
    callback.extend(b"\x31\xc0\xc3")
    failure = len(callback)
    callback.extend(b"\xc7\x05" + struct.pack("<II", status, 0xDEAD) + b"\xb8\x01\0\0\0\xc3")
    struct.pack_into("<i", callback, success_branch, success - success_branch - 4)
    for branch in failures:
        struct.pack_into("<i", callback, branch, failure - branch - 4)
    g.code = bytearray(b"\xe9" + struct.pack("<I", len(callback))) + callback

    def store(address, value):
        g.code.extend(b"\xc7\x05" + struct.pack("<II", address, value))

    def save(address):
        g.code.extend(b"\xa3" + struct.pack("<I", address))

    g.call(2, 763, 0)
    g.result(1, 10)
    g.call(2, 716, 1, 0)
    g.call(2, 926, 1, class_name, 0x10005, 0, 8)
    g.result(1, 11)
    g.call(2, 909, 1, class_name, title, 0, 4, 5, 30, 20, 1, 3, 1234, control, 0)
    save(window)
    g.word(status, 1, 12)
    g.call_handle(2, 800, window)
    g.result(1, 13)
    g.call_handle(2, 805, window, 64, output)
    g.result(4, 14)
    g.word(output, int.from_bytes(b"Wire", "little"), 15)
    g.call_handle(2, 834, window, 5)
    g.result(1, 16)
    g.call(2, 702, 1)
    save(enumeration)
    g.call_handle(2, 756, enumeration)
    g.require(b"\x3b\x05" + struct.pack("<I", window), 17)
    g.call_handle(2, 756, enumeration)
    g.result(0, 18)
    g.call_handle(2, 756, enumeration)
    g.require(b"\x3b\x05" + struct.pack("<I", window), 19)
    g.call_handle(2, 737, enumeration)
    g.result(1, 20)
    g.call_handle(2, 757, window)
    save(space)
    g.call(3, 586, (space,), 1, helv, count, 228, 0)
    g.result(4, 30)
    g.word(count, 0, 31)
    store(count, 2)
    g.call(3, 586, (space,), 1, helv, count, 228, metrics)
    g.result(2, 32)
    g.word(count, 2, 33)
    g.word(metrics, int.from_bytes(b"Helv", "little"), 34)
    g.call_handle(2, 938, window, 15, 7, font_name)
    g.result(1, 35)
    g.call(2, 939, (window,), 15, 0, count, 64, output, 1)
    g.result(7, 36)
    g.word(count, 15, 37)
    g.call(3, 453, (space,), 228, metrics)
    g.result(1, 38)
    g.word(metrics + 68, 12, 39)  # nine points at logical 96 dpi
    g.call(3, 489, (space,), 4, text, 5, output)
    g.result(1, 40)
    g.word(output + 36, 0, 41)  # concatenation point stays on the baseline
    g.call(3, 606, 1, 37, 2, output)
    g.result(1, 42)
    g.word(output, 96, 43)
    g.word(output + 4, 96, 44)
    for identity in (701, 702, 703):
        g.call(3, 399, (space,), 0, identity, 0, 0)
        g.code.extend(b"\x85\xc0\x75\x12")
        g.exit(45)
        g.code.extend(b"\xa3" + struct.pack("<I", bitmap))
        g.call(3, 573, (bitmap,), output)
        g.result(1, 46)
        g.word(output + 4, 0x00010002, 47)
        g.word(output + 8, 0x00180001, 48)
        g.call(3, 371, (bitmap,))
        g.result(1, 49)
    if checked:
        g.call(3, 586, (space,), 1, helv, 0xDEADBEEF, 228, 0)
        g.result(0xFFFFFF01, 50)
        g.call(3, 453, (space,), 228, 0xDEADBEEF)
        g.result(0, 51)
        g.call(3, 489, (space,), 4, 0xDEADBEEF, 5, output)
        g.result(0, 52)
        g.call(2, 938, (window,), 15, 7, 0xDEADBEEF)
        g.result(0, 53)
    g.call_handle(2, 848, space)
    g.result(1, 60)
    g.call_handle(2, 728, window)
    g.result(1, 61)
    g.call(2, 888, 1)
    g.result(1, 62)
    g.exit(0)
    resources = []
    for identity, fixed in ((701, 12), (702, 40), (703, 64)):
        header = bytearray(fixed)
        struct.pack_into("<I", header, 0, fixed)
        if fixed == 12:
            struct.pack_into("<HHHH", header, 4, 2, 1, 1, 24)
        else:
            struct.pack_into("<IIHH", header, 4, 2, 1, 1, 24)
        bits = bytes((0, 0, 255, 0, 255, 0, 0, 0))
        resource = struct.pack("<2sIHHI", b"BM", 14 + fixed + len(bits), 0, 0, 14 + fixed)
        resources.append((2, identity, resource + header + bits))
    return linear_image(
        g.code,
        g.fixups,
        ["DOSCALLS", "PMWIN", "PMGPI"],
        "WINDOWS",
        data=g.data,
        resources=resources,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-windows-") as temporary:
        work = Path(temporary)
        executable = work / "windows.exe"
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
                f"window exit={result.returncode}\n" + result.stderr.decode(errors="replace")
            )
        print(
            "guest CREATESTRUCT callbacks, window enumeration, presentation attributes and measured fonts passed"
        )


if __name__ == "__main__":
    main()
