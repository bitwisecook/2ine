#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise DIVE's fixed-width guest structures, mapped buffers and bad pointers."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def program(checked):
    g = FlatGuest()
    instance = g.field(bytes(4))
    image = g.field(bytes(4))
    pointer = g.field(bytes(4))
    stride = g.field(bytes(4))
    lines = g.field(bytes(4))
    formats = g.field(bytes(256))
    association = g.field(b"kept" + bytes(12))
    caps = bytearray(56)
    struct.pack_into("<I", caps, 0, 56)
    struct.pack_into("<II", caps, 48, 256, formats)
    caps_address = g.field(caps)
    g.call(2, 1, caps_address, 0)
    g.result(0, 10)
    g.word(caps_address + 16, 32, 11)
    g.word(caps_address + 52, formats, 12)
    g.word(formats, 0x3854554C, 13)
    g.call(2, 16, 0, 0)  # VOID aperture notifications have no guest return value.
    g.call(2, 17)
    g.call(2, 2, instance, 0, 0)
    g.result(0, 14)
    g.call(2, 12, (instance,), image, 0x3854554C, 3, 2, 0, 0)
    g.result(0, 15)
    g.call(2, 14, (instance,), (image,), pointer, stride, lines)
    g.result(0, 16)
    g.word(stride, 4, 17)
    g.word(lines, 2, 18)
    g.load(pointer)
    g.code.extend(b"\xc7\x00test")
    g.call(2, 15, (instance,), (image,))
    g.result(0, 19)
    g.call(2, 14, (instance,), (image,), pointer, stride, lines)
    g.result(0, 20)
    g.load(pointer)
    g.require(b"\x81\x38test", 21)
    g.call(2, 15, (instance,), (image,))
    g.result(0, 22)
    g.call(2, 13, (instance,), (image,))
    g.result(0, 23)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", image, 0))
    g.call(2, 12, (instance,), image, 0x3854554C, 3, 2, 4, association)
    g.result(0, 24)
    g.call(2, 14, (instance,), (image,), pointer, stride, lines)
    g.result(0, 25)
    g.word(pointer, association, 26)
    g.load(pointer)
    g.require(b"\x81\x38kept", 27)
    g.call(2, 15, (instance,), (image,))
    g.result(0, 28)
    if checked:
        g.call(2, 14, (instance,), (image,), 0xDEADBEEF, stride, lines)
        g.result(0x1010, 30)
        g.call(2, 14, (instance,), (image,), pointer, stride, lines)
        g.result(0, 31)
        g.call(2, 15, (instance,), (image,))
        g.result(0, 32)
        g.call(2, 10, (instance,), 0, 256, 0xDEADBEEF)
        g.result(0x100E, 33)
        g.call(2, 1, 0xDEADBEEF, 0)
        g.result(0x1004, 34)
    g.call(2, 3, (instance,))
    g.result(0, 40)
    g.call(2, 3, (instance,))
    g.result(0x1000, 41)
    g.word(association, int.from_bytes(b"kept", "little"), 42)
    # Non-screen instances perform checked RGB conversion without a PM owner.
    src_number = g.field(bytes(4))
    dst_number = g.field(bytes(4))
    rgb = g.field(bytes([255, 0, 0, 0, 0, 255, 0, 0]))
    bgr = g.field(bytes(8))
    rect = g.field(struct.pack("<4I", 0, 0, 2, 1))
    mask = g.field(bytes([255]))
    setup = g.field(
        struct.pack(
            "<17I", 68, 1, 0x34424752, 2, 1, 0, 0, 0, 0x34524742, 2, 1, 0, 0, 0, 0, 0xFFFFFFFF, 0
        )
    )
    short_setup = g.field(struct.pack("<2I", 8, 1))
    g.call(2, 2, instance, 1, pointer)
    g.result(0, 43)
    g.word(pointer, 0, 44)
    g.call(2, 6, (instance,), rect)
    g.result(0x1006, 45)
    g.call(2, 7, (instance,), 0)
    g.result(0x1006, 46)
    g.call(2, 8, (instance,))
    g.result(0x1009, 47)
    g.call(2, 11, (instance,), rect, pointer, stride, lines)
    g.result(0x1006, 48)
    g.call(2, 12, (instance,), src_number, 0x34424752, 2, 1, 8, rgb)
    g.result(0, 49)
    g.call(2, 12, (instance,), dst_number, 0x34524742, 2, 1, 8, bgr)
    g.result(0, 50)
    g.call(2, 4, (instance,), setup)
    g.result(0, 51)
    g.call(2, 4, (instance,), short_setup)
    g.result(0, 52)
    g.call(2, 19, (instance,), (src_number,), (dst_number,), mask)
    g.result(0, 53)
    g.word(bgr, 0x00FF0000, 54)
    g.word(bgr + 4, 0x0000FF00, 55)
    g.call(2, 18, (instance,), 1, 0x00FF0000, 0)
    g.result(0, 56)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", bgr, 0x00112233))
    g.call(2, 5, (instance,), (src_number,), (dst_number,))
    g.result(0, 57)
    g.word(bgr, 0x00112233, 58)
    if checked:
        g.call(2, 19, (instance,), (src_number,), (dst_number,), 0xDEADBEEF)
        g.result(0x101B, 59)
        g.call(2, 4, (instance,), short_setup + 0x10000000)
        g.result(0x1004, 60)
    g.call(2, 3, (instance,))
    g.result(0, 61)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "DIVE"], "DIVETEST", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-dive-") as temporary:
        work = Path(temporary)
        executable = work / "dive.exe"
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
                f"DIVE exit={result.returncode}\n" + result.stderr.decode(errors="replace")
            )
        print("DIVE guest structures, mapped/associated buffers and checked spans passed")


if __name__ == "__main__":
    main()
