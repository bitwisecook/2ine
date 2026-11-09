#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Check original Open32 interlocked signs, direct backing and guarded LX ABI."""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from tests.integration.harness.images import FlatGuest, linear_image


def program():
    guest = FlatGuest()
    stack = guest.field(bytes(4))
    guest.code.extend(b"\x89\x25" + struct.pack("<I", stack))
    guard = guest.field(b"\xa5" + struct.pack("<I", 123) + b"\xa5")
    word = guard + 1  # x86 permits unaligned synchronization words.
    guest.call(2, 624, 0x5151)
    guest.call(2, 423, word)
    guest.result(1, 1)
    guest.word(word, 124, 2)  # Return is sign, not the stored value124.
    guest.call(2, 422, word, 0x7FFFFFFF)
    guest.result(124, 3)
    guest.call(2, 423, word)
    guest.result(0xFFFFFFFF, 4)
    guest.word(word, 0x80000000, 5)
    guest.call(2, 421, word)
    guest.result(1, 6)
    guest.word(word, 0x7FFFFFFF, 7)
    guest.call(2, 422, word, 0xFFFFFFFF)
    guest.result(0x7FFFFFFF, 8)
    guest.call(2, 423, word)
    guest.result(0, 9)
    guest.word(word, 0, 10)
    guest.call(2, 421, word)
    guest.result(0xFFFFFFFF, 11)
    guest.call(2, 422, word, 1)
    guest.result(0xFFFFFFFF, 12)
    guest.call(2, 421, word)
    guest.result(0, 13)
    guest.word(word, 0, 14)
    guest.call(2, 300)
    guest.result(0x5151, 15)
    guest.require(b"\x80\x3d" + struct.pack("<I", guard) + b"\xa5", 16)
    guest.require(b"\x80\x3d" + struct.pack("<I", word + 4) + b"\xa5", 17)
    cross_page = 0x20FFF
    guest.call(2, 422, cross_page, 0x123)
    guest.result(0, 18)
    guest.call(2, 423, cross_page)
    guest.result(1, 19)
    guest.word(cross_page, 0x124, 20)
    guest.require(b"\x80\x3d" + struct.pack("<I", cross_page - 1) + b"\0", 21)
    guest.require(b"\x80\x3d" + struct.pack("<I", cross_page + 4) + b"\0", 22)
    for address in (0, 0xDEADBEEF, 0xFFFFFFFE, 0x2FFFF, 0x10000, 0x10FFF):
        for ordinal, tail in ((421, ()), (422, (777,)), (423, ())):
            guest.call(2, ordinal, address, *tail)
            guest.result(0, 23)
            guest.call(2, 300)
            guest.result(87, 24)
    guest.word(word, 0, 25)
    guest.word(cross_page, 0x124, 26)
    guest.word(0x10000, 0xC03103EB, 27)  # Read-only code was not overwritten.
    guest.require(b"\x3b\x25" + struct.pack("<I", stack), 28)
    guest.exit(0)
    return linear_image(guest.code, guest.fixups, ["DOSCALLS", "PMWINX"], "ATOMIC", data=guest.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="atomic-", dir=args.work.resolve()) as temporary:
        work = Path(temporary)
        executable = work / "atomic.exe"
        executable.write_bytes(program())
        result = subprocess.run(
            [str(build / "lx_loader"), str(executable)],
            cwd=work,
            env=dict(
                os.environ,
                TWOINE_BACKEND=args.backend,
                TWOINE_LIBPATH=str(build),
                LD_LIBRARY_PATH=str(build),
                SDL_VIDEO_DRIVER="dummy",
                SDL_RENDER_DRIVER="software",
            ),
            capture_output=True,
            timeout=20,
        )
        if result.returncode:
            raise RuntimeError(
                f"Open32 interlocked exit={result.returncode}\n"
                + result.stderr.decode(errors="replace")
            )
    print("Open32 interlocked LX signs, direct backing, guarded spans and4/8-byte cleanup passed")


if __name__ == "__main__":
    main()
