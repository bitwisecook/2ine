#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Check counted Unicode bridges, wire widths, guest guards and caller cleanup."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def byte(guest, address, expected, status):
    guest.require(b"\x80\x3d" + struct.pack("<I", address) + bytes([expected]), status)


def save_stack(guest):
    saved = guest.field(bytes(4))
    guest.code.extend(b"\x89\x25" + struct.pack("<I", saved))
    return saved


def finish(guest, saved, name):
    guest.require(b"\x3b\x25" + struct.pack("<I", saved), 90)
    guest.exit(0)
    return linear_image(guest.code, guest.fixups, ["DOSCALLS", "PMWINX"], name, data=guest.data)


def conversions():
    guest = FlatGuest()
    saved = save_stack(guest)
    source = guest.field(b"A\x80\xe9\0")
    unicode_bmp = guest.field(struct.pack("<4H", ord("A"), 0x20AC, 0xE9, 0))
    unicode_pair = guest.field(struct.pack("<3H", 0xD83D, 0xDE00, 0))
    unicode_snow = guest.field(struct.pack("<2H", 0x2603, 0))
    emoji = guest.field(b"\xf0\x9f\x98\x80\0")
    invalid_utf8 = guest.field(b"\xc0\xaf")
    replacement = guest.field(b"XY\0")
    wide = guest.field(bytes([0xA5]) * 32)
    output = guest.field(bytes([0xA5]) * 32)
    used = guest.field(bytes([0xA5]) * 8)
    guest.call(2, 624, 0x5151)
    guest.call(2, 2073, 0, 1, source, 0xFFFFFFFF, source, 0)
    guest.result(4, 1)  # Query ignores destination even when it equals input.
    guest.word(source, 0x00E98041, 2)
    guest.call(2, 2073, 0, 1, source, 0xFFFFFFFF, wide + 1, 4)
    guest.result(4, 3)
    guest.word(wide + 1, 0x20AC0041, 4)
    guest.word(wide + 5, 0x000000E9, 5)
    byte(guest, wide, 0xA5, 6)
    byte(guest, wide + 9, 0xA5, 7)
    guest.call(2, 300)
    guest.result(0x5151, 8)
    guest.call(2, 2079, 0, 0, unicode_bmp, 0xFFFFFFFF, unicode_bmp, 0, 0, used + 1)
    guest.result(4, 9)
    guest.word(used + 1, 0, 10)
    guest.call(2, 2079, 0, 0, unicode_bmp, 0xFFFFFFFF, output + 1, 4, 0, used + 1)
    guest.result(4, 11)
    guest.word(output + 1, 0x00E98041, 12)
    byte(guest, output, 0xA5, 13)
    byte(guest, output + 5, 0xA5, 14)
    guest.call(2, 2073, 65001, 8, emoji, 0xFFFFFFFF, wide + 1, 3)
    guest.result(3, 15)
    guest.word(wide + 1, 0xDE00D83D, 16)
    guest.call(2, 2079, 65001, 0, unicode_pair, 0xFFFFFFFF, output + 1, 5, 0, used + 1)
    guest.result(5, 17)
    guest.word(output + 1, 0x80989FF0, 18)
    byte(guest, output + 5, 0, 19)
    guest.word(used + 1, 0, 20)
    byte(guest, used, 0xA5, 21)
    byte(guest, used + 5, 0xA5, 22)
    guest.call(2, 2079, 65001, 0, unicode_pair, 2, 0, 0, 0, 0)
    guest.result(4, 23)  # Positive count excludes the NUL.
    guest.call(2, 2079, 1252, 0, unicode_snow, 0xFFFFFFFF, output + 1, 3, replacement, used + 1)
    guest.result(3, 24)
    byte(guest, output + 1, ord("X"), 25)
    byte(guest, output + 2, ord("Y"), 26)
    byte(guest, output + 3, 0, 27)
    guest.word(used + 1, 1, 28)
    guest.call(2, 2073, 65001, 8, invalid_utf8, 2, wide + 1, 3)
    guest.result(0, 29)
    guest.call(2, 300)
    guest.result(1113, 30)
    guest.word(wide + 1, 0xDE00D83D, 31)
    embedded = guest.field(b"A\0B")
    embedded_output = guest.field(bytes([0xA5]) * 8)
    guest.call(2, 2073, 1252, 0, embedded, 3, embedded_output, 3)
    guest.result(3, 32)
    guest.word(embedded_output, 0x00000041, 33)
    guest.word(embedded_output + 4, 0xA5A50042, 34)
    return finish(guest, saved, "UNICODE")


def guards():
    guest = FlatGuest()
    saved = save_stack(guest)
    source = guest.field(b"abc\0")
    wide = guest.field(struct.pack("<3H", ord("a"), ord("b"), 0))
    output = guest.field(bytes([0xA5]) * 32)
    empty = guest.field(b"\0")
    too_long = guest.field(b"12345678901234\0")
    used = guest.field(bytes([0xA5]) * 8)
    forward = [
        ((0, 0, 0xDEADBEEF, 1, 0, 0), 87),
        ((0, 0, 0xFFFFFFFE, 3, output, 3), 87),
        ((0, 0, 0x2FFFF, 2, output, 3), 87),
        ((0, 0, source, 3, 0xDEADBEEF, 3), 87),
        ((0, 0, source, 3, 0xFFFFFFFE, 3), 87),
        ((0, 0, source, 3, 0x2FFFF, 3), 87),
        ((0, 0, source, 3, source, 3), 87),
        ((0, 0, source, 3, source + 1, 3), 87),
        ((0, 0, source, 3, output, 2), 122),
        ((0, 3, source, 3, output, 3), 1004),
        ((0, 16, source, 3, output, 3), 1004),
        ((0, 2, source, 3, output, 3), 50),
        ((932, 0, source, 3, output, 3), 50),
        ((0, 0, source, 3, output, 0xFFFFFFFF), 87),
        ((0, 0, source, 0xFFFFFFFE, output, 3), 87),
    ]
    for arguments, error in forward:
        guest.call(2, 2073, *arguments)
        guest.result(0, 40)
        guest.call(2, 300)
        guest.result(error, 41)
    reverse = [
        ((0, 0, 0xDEADBEEF, 1, output, 3, 0, 0), 87),
        ((0, 0, 0xFFFFFFFE, 2, output, 3, 0, 0), 87),
        ((0, 0, 0x2FFFF, 1, output, 3, 0, 0), 87),
        ((0, 0, wide, 2, 0xFFFFFFFE, 3, 0, 0), 87),
        ((0, 0, wide, 2, wide, 3, 0, 0), 87),
        ((0, 0, wide, 2, wide + 1, 3, 0, 0), 87),
        ((0, 0, wide, 2, output, 3, 0, 0xDEADBEEF), 87),
        ((0, 0, wide, 2, output, 3, 0, 0x2FFFF), 87),
        ((0, 0, wide, 2, output, 3, 0xDEADBEEF, 0), 87),
        ((0, 0, wide, 2, output, 3, empty, 0), 1359),
        ((0, 0, wide, 2, output, 3, too_long, 0), 1359),
        ((0, 0, wide, 2, output, 0xFFFFFFFF, 0, 0), 122),
        ((0, 0x100, wide, 2, output, 3, 0, 0), 1004),
        ((0, 0x10, wide, 2, output, 3, 0, 0), 1004),
        ((0, 0x250, wide, 2, output, 3, 0, 0), 1004),
        ((0, 0x200, wide, 2, output, 3, 0, 0), 50),
    ]
    for arguments, error in reverse:
        guest.call(2, 2079, *arguments)
        guest.result(0, 42)
        guest.call(2, 300)
        guest.result(error, 43)
    guest.word(source, 0x00636261, 44)
    guest.word(wide, 0x00620061, 45)
    guest.word(output, 0xA5A5A5A5, 46)
    guest.word(used, 0xA5A5A5A5, 47)
    return finish(guest, saved, "UNIGUARD")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    for name, image in (("conversions", conversions()), ("guards", guards())):
        with tempfile.TemporaryDirectory(prefix="unicode-", dir=args.work.resolve()) as temporary:
            work = Path(temporary)
            executable = work / (name + ".exe")
            executable.write_bytes(image)
            environment = dict(
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
                env=environment,
                capture_output=True,
                timeout=20,
            )
            if result.returncode:
                raise RuntimeError(
                    f"Open32 Unicode {name} exit={result.returncode}\n"
                    + result.stderr.decode(errors="replace")
                )
    print("Open32 Unicode LX conversions, guarded spans and 24/32-byte caller cleanup passed")


if __name__ == "__main__":
    main()
