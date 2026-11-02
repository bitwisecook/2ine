#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Release nested process critical sections when flat or segmented workers exit."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image
from legacy import Guest as SegmentedGuest


def flat_program():
    g = FlatGuest()
    thread, state = g.field(bytes(4)), g.field(bytes(4))
    g.call(1, 233)
    g.result(485, 10)
    for expected in (1, 2):
        g.call(1, 311, thread, 0x77665544, 0, 0, 65536)
        g.result(0, 11)
        g.call(1, 349, thread, 0)
        g.result(0, 12)
        g.word(state, expected, 13)
    g.call(1, 232)
    g.result(0, 14)
    g.call(1, 233)
    g.result(0, 15)
    g.call(1, 233)
    g.result(485, 16)
    g.exit(0)
    workers = []
    for value in (1, 2):
        workers.append(0x10000 + len(g.code))
        for _ in range(2):
            g.call(1, 232)
            g.result(0, 17)
        g.code.extend(b"\xc7\x05" + struct.pack("<II", state, value))
        g.call(1, 234, 0, 0)
        g.exit(18)
    # Each call's unique marker is the thread procedure argument.
    for worker in workers:
        marker = g.code.index(struct.pack("<I", 0x77665544))
        struct.pack_into("<I", g.code, marker, worker)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "CRIT32", data=g.data)


def segmented_program():
    g = SegmentedGuest()
    thread, state = g.field(bytes(2)), g.field(bytes(2))
    semaphore = g.field(struct.pack("<I", 1))
    stacks = [g.field(bytes(4096)) + 4094 for _ in range(2)]
    entries = []
    g.call(1, 6)
    g.require(b"\x3d\xe5\x01", 20)  # ERROR_CRITSEC_UNDERFLOW.
    for value, stack in enumerate(stacks, 1):
        if value == 2:
            g.pointer(semaphore)
            g.call(1, 143)
            g.zero(21)
        g.code.extend(b"\x0e")  # Code selector for the worker entry point.
        g.push(0x7766)
        entries.append(len(g.code) - 2)
        g.pointer(thread)
        g.pointer(stack)
        g.call(1, 145)
        g.zero(22)
        g.pointer(semaphore)
        g.code.extend(b"\x66\x68\xff\xff\xff\xff")
        g.call(1, 142)
        g.zero(23)
        g.word(state, value, 24)
    g.call(1, 3)
    g.zero(25)
    g.call(1, 6)
    g.zero(26)
    g.exit(0)
    for value, entry in enumerate(entries, 1):
        struct.pack_into("<H", g.code, entry, len(g.code))
        for _ in range(2):
            g.call(1, 3)
            g.zero(27)
        g.code.extend(b"\xc7\x06" + struct.pack("<HH", state, value))
        g.pointer(semaphore)
        g.call(1, 141)
        g.zero(28)
        g.push(0)
        g.push(0)
        g.call(1, 5)
        g.exit(29)
    return g.image(False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-critical-sections-") as temporary:
        work = Path(temporary)
        for name, image in (("flat", flat_program()), ("segmented", segmented_program())):
            executable = work / (name + ".exe")
            executable.write_bytes(image)
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
                    f"{name} critical-section guest exit={result.returncode}\n"
                    + result.stderr.decode(errors="replace")
                )
        print("Flat and segmented workers release nested critical sections on thread exit")


if __name__ == "__main__":
    main()
