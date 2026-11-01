#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Verify stock mux wait records, lifetime, ANY/ALL results and worker wakeups."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def store(g, address):
    g.code.extend(b"\xa3" + struct.pack("<I", address))


def records(g, handles):
    output = g.field(bytes(8 * len(handles)))
    for i, handle in enumerate(handles):
        g.load(handle)
        store(g, output + i * 8)
        g.code.extend(b"\xc7\x05" + struct.pack("<II", output + i * 8 + 4, (i + 1) * 11))
    return output


def events_program(checked):
    g = FlatGuest()
    first, second, any_mux, all_mux, empty, user, count, flags, mutex = (
        g.field(bytes(4)) for _ in range(9)
    )
    output = g.field(bytes(32))
    g.call(1, 324, 0, first, 0, 0)
    g.result(0, 1)
    g.call(1, 324, 0, second, 0, 1)
    g.result(0, 2)
    r = records(g, (first, second))
    g.call(1, 337, 0, any_mux, 2, r, 0)
    g.result(87, 3)
    g.call(1, 337, 0, any_mux, 2, r, 2)
    g.result(0, 4)
    g.call_handle(1, 340, any_mux, 0, user)
    g.result(0, 5)
    g.word(user, 22, 6)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", count, 1))
    g.call_handle(1, 343, any_mux, count, output, flags)
    g.result(289, 7)
    g.word(count, 2, 8)
    g.word(flags, 0, 9)
    g.call_handle(1, 343, any_mux, count, output, flags)
    g.result(0, 10)
    g.word(flags, 2, 11)
    g.word(output + 4, 11, 12)
    g.word(output + 12, 22, 13)
    g.call_handle(1, 341, any_mux, r)
    g.result(284, 14)
    g.call_handle(1, 342, any_mux, 0xFFFFFFFF)
    g.result(6, 15)
    g.call(1, 337, 0, all_mux, 2, r, 4)
    g.result(0, 16)
    g.call_handle(1, 340, all_mux, 0, user)
    g.result(640, 17)
    g.word(user, 22, 18)
    g.call(2, 978, (first,), 0)
    g.result(640, 19)
    g.call(2, 978, (second,), 0)
    g.result(0, 20)
    g.call(1, 328, (first,))
    g.result(0, 21)
    g.call(2, 980, (all_mux,), 0, user)
    g.result(0, 22)
    g.word(user, 22, 23)
    g.call_handle(1, 342, all_mux, (first,))
    g.result(0, 24)
    g.call_handle(1, 342, all_mux, (second,))
    g.result(0, 25)
    g.call_handle(1, 340, all_mux, 0, user)
    g.result(286, 26)
    g.call(1, 337, 0, empty, 0, 0, 2)
    g.result(0, 27)
    g.call_handle(1, 340, empty, 0, user)
    g.result(286, 28)
    if checked:
        g.call_handle(1, 340, any_mux, 0, 0xFFFFFFF0)
        g.result(87, 29)
        g.call(1, 337, 0, empty, 1, 0xFFFFFFF0, 2)
        g.result(87, 30)
    g.call(1, 331, 0, mutex, 0, 0)
    g.result(0, 31)
    mixed = records(g, (second, mutex))
    g.call(1, 337, 0, output, 2, mixed, 2)
    g.result(292, 32)
    g.call(2, 979, (mutex,), 0)
    g.result(0, 33)
    g.call(1, 335, (mutex,))
    g.result(0, 34)
    g.call(1, 333, (mutex,))
    g.result(0, 35)
    # Closing the public event does not free the mux's retained member.
    g.call(1, 326, (first,))
    g.result(0, 36)
    g.call_handle(1, 340, any_mux, 0, user)
    g.result(0, 37)
    g.word(user, 11, 38)
    for handle in (any_mux, all_mux, empty):
        g.call_handle(1, 339, handle)
        g.result(0, 39)
    g.call(1, 326, (second,))
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWIN"], "MUXEVENT", data=g.data)


def named_program():
    g = FlatGuest()
    event, mux, opened, user, count, flags = (g.field(bytes(4)) for _ in range(6))
    name, alias = g.field(b"\\SEM32\\MuxFixture\0"), g.field(b"\\sem32\\muxfixture\0")
    output = g.field(bytes(8))
    g.call(1, 324, 0, event, 0, 1)
    r = records(g, (event,))
    g.call(1, 337, name, mux, 1, r, 2)
    g.result(292, 1)
    g.call(1, 326, (event,))
    g.call(1, 324, 0, event, 1, 1)
    g.load(event)
    store(g, r)
    g.call(1, 337, 0, mux, 1, r, 2)
    g.result(0, 2)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", count, 1))
    g.call_handle(1, 343, mux, count, output, flags)
    g.result(0, 3)
    g.word(flags, 2, 4)
    g.call_handle(1, 339, mux)
    g.call(1, 337, name, mux, 1, r, 2)
    g.result(0, 5)
    g.call(1, 338, alias, opened)
    g.result(0, 6)
    g.load(opened)
    g.require(b"\x3b\x05" + struct.pack("<I", mux), 7)
    g.call_handle(1, 339, opened)
    g.result(0, 8)
    g.call_handle(1, 340, mux, 0, user)
    g.result(0, 9)
    g.word(user, 11, 10)
    g.call_handle(1, 343, mux, count, output, flags)
    g.result(0, 11)
    g.word(flags, 3, 12)
    g.call_handle(1, 339, mux)
    g.result(0, 13)
    g.call(1, 338, alias, opened)
    g.result(187, 14)
    g.call(1, 326, (event,))
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "MUXNAMED", data=g.data)


def worker_program(mutex):
    g = FlatGuest()
    sem, mux, thread, started, user = (g.field(bytes(4)) for _ in range(5))
    g.call(1, 331 if mutex else 324, 0, sem, 0, 0)
    g.result(0, 1)
    g.call(1, 324, 0, started, 0, 0)
    r = records(g, (sem,))
    g.call(1, 337, 0, mux, 1, r, 2)
    g.result(0, 2)
    g.call(1, 311, thread, 0x88776655, 0, 0, 65536)
    g.result(0, 3)
    g.call(1, 329, (started,), 3000)
    g.result(0, 4)
    g.call_handle(1, 340, mux, 3000, user)
    g.result(0, 5)
    g.word(user, 11, 6)
    if mutex:
        g.call(1, 335, (sem,))
        g.result(0, 7)
    g.call(1, 349, thread, 0)
    g.result(0, 8)
    g.call_handle(1, 339, mux)
    g.result(0, 9)
    g.call(1, 333 if mutex else 326, (sem,))
    g.result(0, 10)
    g.call(1, 326, (started,))
    g.exit(0)
    worker = 0x10000 + len(g.code)
    if mutex:
        g.call(1, 334, (sem,), 0)
        g.result(0, 11)
    g.call(1, 328, (started,))
    g.call(1, 229, 30)
    g.call(1, 335 if mutex else 328, (sem,))
    g.result(0, 12)
    g.code.extend(b"\xc3")
    g.code = g.code.replace(struct.pack("<I", 0x88776655), struct.pack("<I", worker))
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "MUXWORK", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    env = dict(
        os.environ,
        TWOINE_BACKEND=args.backend,
        TWOINE_LIBPATH=str(build),
        LD_LIBRARY_PATH=str(build),
    )
    with tempfile.TemporaryDirectory(prefix="twoine-mux-") as temporary:
        path = Path(temporary) / "mux.exe"
        for payload in (
            events_program(args.backend == "unicorn"),
            named_program(),
            worker_program(False),
            worker_program(True),
        ):
            path.write_bytes(payload)
            result = subprocess.run(
                [str(build / "lx_loader"), str(path)], env=env, capture_output=True, timeout=15
            )
            if result.returncode:
                raise RuntimeError(
                    f"mux exit={result.returncode}\n" + result.stderr.decode(errors="replace")
                )
    print("Mux ANY/ALL records, retained members, named opens and worker notifications passed")


if __name__ == "__main__":
    main()
