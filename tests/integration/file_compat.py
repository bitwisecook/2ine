#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise legacy FIL_STANDARD guest prefixes, canaries and checked spans."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def program(checked):
    g = FlatGuest()
    name = g.field(b"sample.txt\0")
    absent = g.field(b"absent.txt\0")
    handle = g.field(bytes(4))
    action = g.field(bytes(4))
    g.call(1, 273, name, handle, action, 0, 0, 1, 0x40, 0)
    g.result(0, 10)
    g.word(action, 1, 11)

    failure = 20
    for ordinal, source in ((223, name), (279, (handle,))):
        for size in (21, 22, 23, 24, 30):
            allocation = g.field(b"HEAD" + bytes([0xCC]) * 32 + b"TAIL")
            output = allocation + 4
            g.call(1, ordinal, source, 1, output, size)
            g.result(111 if size < 22 else 0, failure)
            g.word(allocation, int.from_bytes(b"HEAD", "little"), failure + 1)
            g.word(output + 32, int.from_bytes(b"TAIL", "little"), failure + 2)
            written = 0 if size < 22 else min(size, 24)
            g.word(output + written, 0xCCCCCCCC, failure + 3)
            if size >= 22:
                g.word(output + 12, 5, failure + 4)
                # Attributes occupy a two-byte prefix even with a 22-byte caller.
                g.require(
                    b"\x0f\xb7\x05" + struct.pack("<I", output + 20) + b"\x3d\0\0\0\0",
                    failure + 5,
                )
            else:
                g.word(output, 0xCCCCCCCC, failure + 4)
            failure += 6

        # Exactly 22 bytes remain in the guest data object. A bridge must borrow
        # the requested legacy prefix, rather than a full 24-byte FILESTATUS3.
        boundary = 0x30000 - 22
        g.call(1, ordinal, source, 1, boundary, 22)
        g.result(0, failure)
        g.word(boundary + 12, 5, failure + 1)
        g.require(
            b"\x0f\xb7\x05" + struct.pack("<I", boundary + 20) + b"\x3d\0\0\0\0",
            failure + 2,
        )
        failure += 3
        if checked:
            g.call(1, ordinal, source, 1, boundary + 1, 22)
            g.result(87, failure)
            g.call(1, ordinal, source, 1, 0xDEADBEEF, 22)
            g.result(87, failure + 1)
            failure += 2

        rejected = g.field(bytes([0xCC]) * 32)
        g.call(1, ordinal, source, 999, rejected, 22)
        g.result(124, failure)
        g.word(rejected, 0xCCCCCCCC, failure + 1)
        failure += 2

    untouched = g.field(bytes([0xCC]) * 32)
    g.call(1, 223, absent, 1, untouched, 22)
    g.result(2, 110)
    g.word(untouched, 0xCCCCCCCC, 111)
    g.call(1, 279, 0xFFFFFFFE, 1, untouched, 22)
    g.result(6, 112)
    g.word(untouched, 0xCCCCCCCC, 113)
    if checked:
        g.call(1, 223, 0xDEADBEEF, 1, untouched, 22)
        g.result(87, 114)
        g.word(untouched, 0xCCCCCCCC, 115)
    g.call(1, 257, (handle,))
    g.result(0, 116)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "FILECOMPAT", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-file-compat-") as temporary:
        work = Path(temporary)
        (work / "sample.txt").write_bytes(b"hello")
        executable = work / "file_compat.exe"
        executable.write_bytes(program(args.backend == "unicorn"))
        result = subprocess.run(
            [str(build / "lx_loader"), str(executable)],
            cwd=work,
            env={
                **os.environ,
                "TWOINE_BACKEND": args.backend,
                "TWOINE_LIBPATH": str(build),
                "LD_LIBRARY_PATH": str(build),
            },
            capture_output=True,
            timeout=15,
        )
        if result.returncode:
            raise RuntimeError(
                f"file compatibility guest exit={result.returncode}\n"
                + result.stderr.decode(errors="replace")
            )
        print("22-byte guest file status, boundary spans and caller canaries passed")


if __name__ == "__main__":
    main()
