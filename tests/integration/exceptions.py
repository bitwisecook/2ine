#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise OS/2 exception registration, unwind notifications and continuation."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def program():
    g = FlatGuest()
    records = [g.field(struct.pack("<3I", 0, 0x88776655, digit)) for digit in (1, 2, 3)]
    sequence, expected_flags, stack = (g.field(bytes(4)) for _ in range(3))
    for record in records:
        g.call(1, 354, record)
        g.result(0, 1)
    g.call(1, 354, records[1])
    g.result(87, 2)
    g.call(1, 355, records[1])
    g.result(87, 3)
    g.call(1, 357, 0xDEADBEEF, 0x10002, 0)
    g.result(87, 4)
    g.code.extend(b"\x64\xa1\0\0\0\0")
    g.result(records[2], 5)
    for index, record in enumerate(records):
        g.word(record, records[index - 1] if index else 0xFFFFFFFF, 6)
    # The target frame is retained; younger handlers see a partial unwind.
    g.code.extend(b"\xc7\x05" + struct.pack("<II", expected_flags, 2))
    g.code.extend(b"\xbb\x44\x33\x22\x11\xbe\x88\x77\x66\x55\xbf\xcc\xbb\xaa\x99")
    g.code.extend(b"\x89\xe0\xa3" + struct.pack("<I", stack))
    g.call(1, 357, records[0], 0x77665544, 0)
    g.exit(90)  # A successful unwind transfers control instead of returning.
    partial = 0x10000 + len(g.code)
    g.code.extend(b"\x83\xc4\x0c\x89\xe0")
    g.require(b"\x3b\x05" + struct.pack("<I", stack), 7)
    g.word(sequence, 32, 8)
    g.code.extend(b"\x64\xa1\0\0\0\0")
    g.result(records[0], 9)
    g.call(1, 355, records[0])
    g.result(0, 10)
    g.call(1, 355, records[0])
    g.result(87, 11)
    g.call(1, 354, records[0])
    g.result(0, 12)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", expected_flags, 6))
    g.code.extend(b"\xc7\x05" + struct.pack("<II", sequence, 0))
    g.call(1, 357, 0, 0x66554433, 0)
    g.exit(91)
    complete = 0x10000 + len(g.code)
    g.code.extend(b"\x83\xc4\x0c")
    g.word(sequence, 1, 13)
    g.code.extend(b"\x64\xa1\0\0\0\0")
    g.result(0xFFFFFFFF, 14)
    g.exit(0)
    handler = 0x10000 + len(g.code)
    # Inspect the actual OS/2 report and suspended integer context.
    g.code.extend(b"\x8b\x44\x24\x04")
    g.require(b"\x81\x38\x26\0\0\xc0", 15)
    g.code.extend(b"\x8b\x40\x04")
    g.require(b"\x3b\x05" + struct.pack("<I", expected_flags), 16)
    g.code.extend(b"\x8b\x44\x24\x0c")
    for offset, value in ((0, 7), (128, 0x99AABBCC), (132, 0x55667788), (140, 0x11223344)):
        g.require(b"\x81\xb8" + struct.pack("<II", offset, value), 17)
    g.code.extend(b"\x8b\x44\x24\x08\x8b\x50\x08")
    g.code.extend(
        b"\xa1"
        + struct.pack("<I", sequence)
        + b"\x6b\xc0\x0a\x01\xd0\xa3"
        + struct.pack("<I", sequence)
    )
    g.code.extend(b"\x31\xc0\xc3")
    g.data = g.data.replace(struct.pack("<I", 0x88776655), struct.pack("<I", handler))
    for sentinel, target in ((0x77665544, partial), (0x66554433, complete)):
        g.code = g.code.replace(struct.pack("<I", sentinel), struct.pack("<I", target))
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "UNWIND", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-exceptions-") as temporary:
        work = Path(temporary)
        executable = work / "unwind.exe"
        executable.write_bytes(program())
        result = subprocess.run(
            [str(build / "lx_loader"), str(executable)],
            cwd=work,
            env=dict(
                os.environ,
                TWOINE_BACKEND=args.backend,
                TWOINE_LIBPATH=str(build),
                LD_LIBRARY_PATH=str(build),
            ),
            capture_output=True,
            timeout=10,
        )
        if result.returncode:
            raise RuntimeError(
                f"Unwind exit={result.returncode}\n" + result.stderr.decode(errors="replace")
            )


if __name__ == "__main__":
    main()
