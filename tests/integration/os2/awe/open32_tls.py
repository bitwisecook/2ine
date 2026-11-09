#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise original Open32 TLS ordinals with guest workers and opaque values."""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from tests.integration.harness.images import FlatGuest, linear_image


def store(g, address):
    g.code.extend(b"\xa3" + struct.pack("<I", address))


def repeat(g, loop):
    g.code.extend(b"\x0f\x82" + struct.pack("<i", loop - len(g.code) - 6))


def program():
    g = FlatGuest()
    slot, worker_id, ready, release = (g.field(bytes(4)) for _ in range(4))
    count = g.field(struct.pack("<I", 1))
    indices = g.field(bytes(256))
    for index in (64, 0xFFFFFFFF):
        for ordinal, arguments in ((693, (index,)), (694, (index,)), (695, (index, 1))):
            g.call(2, ordinal, *arguments)
            g.result(0, 1)
            g.call(2, 300)
            g.result(87, 2)
    # Get/Set validate the index range, not the allocation bitmap, as Open32 does.
    g.call(2, 695, 63, 0xFEE1DEAD)
    g.result(1, 3)
    g.call(2, 694, 63)
    g.result(0xFEE1DEAD, 4)
    g.call(2, 692)
    store(g, slot)
    store(g, indices)
    g.require(b"\x83\xf8\xff\x0f\x95\xc0\x3c\x01", 5)
    g.call(2, 624, 123)
    g.call(2, 694, (slot,))
    g.result(0, 6)
    g.call(2, 300)
    g.result(0, 7)
    g.call(2, 624, 321)
    g.call(2, 695, (slot,), 0xFEDCBA98)
    g.result(1, 8)
    g.call(2, 300)
    g.result(321, 9)
    g.call(2, 694, (slot,))
    g.result(0xFEDCBA98, 10)
    g.call(2, 300)
    g.result(0, 11)
    # Allocate the other 63 indices: reuse is then forced, not assumed by order.
    loop = len(g.code)
    g.call(2, 692)
    g.code.extend(b"\x89\xc2")  # Preserve the opaque index across the comparison.
    g.require(b"\x83\xf8\xff\x0f\x95\xc0\x3c\x01", 12)
    g.code.extend(b"\x89\xd0")
    g.code.extend(b"\x8b\x0d" + struct.pack("<I", count))
    g.code.extend(b"\x89\x04\x8d" + struct.pack("<I", indices))
    g.code.extend(b"\xff\x05" + struct.pack("<I", count))
    g.code.extend(b"\x83\x3d" + struct.pack("<I", count) + b"\x40")
    repeat(g, loop)
    g.call(2, 692)
    g.result(0xFFFFFFFF, 13)
    g.call(2, 300)
    g.result(8, 14)
    for event in (ready, release):
        g.call(1, 324, 0, event, 0, 0)
        g.result(0, 15)
    marker = 0x77665544
    g.call(1, 311, worker_id, marker, 0, 0, 65536)
    g.result(0, 16)
    g.call(1, 329, (ready,), 5000)
    g.result(0, 17)
    g.call(2, 694, (slot,))
    g.result(0xFEDCBA98, 18)
    g.call(2, 693, (slot,))
    g.result(1, 19)
    g.call(2, 693, (slot,))  # Original in-range double-free acceptance.
    g.result(1, 20)
    g.call(2, 692)
    g.require(b"\x3b\x05" + struct.pack("<I", slot), 21)
    g.call(2, 694, (slot,))
    g.result(0, 22)
    g.call(1, 328, (release,))
    g.result(0, 23)
    g.call(1, 349, worker_id, 0)
    g.result(0, 24)
    # A fresh worker sees no data left behind by the exited worker.
    second_marker = 0x66554433
    g.call(1, 311, worker_id, second_marker, 0, 0, 65536)
    g.result(0, 25)
    g.call(1, 349, worker_id, 0)
    g.result(0, 26)
    g.call(2, 694, (slot,))
    g.result(0, 27)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", count, 0))
    loop = len(g.code)
    g.code.extend(b"\x8b\x0d" + struct.pack("<I", count))
    g.code.extend(b"\xff\x34\x8d" + struct.pack("<I", indices))
    g.call(2, 693)
    g.code.extend(b"\x83\xc4\x04")
    g.result(1, 28)
    g.code.extend(b"\xff\x05" + struct.pack("<I", count))
    g.code.extend(b"\x83\x3d" + struct.pack("<I", count) + b"\x40")
    repeat(g, loop)
    g.call(1, 326, (ready,))
    g.call(1, 326, (release,))
    g.exit(0)
    worker = 0x10000 + len(g.code)
    g.call(2, 300)
    g.result(0, 30)
    g.call(2, 694, (slot,))
    g.result(0, 31)
    g.call(2, 695, (slot,), 0xBADC0FFE)
    g.result(1, 32)
    g.call(2, 694, (slot,))
    g.result(0xBADC0FFE, 33)
    g.call(1, 328, (ready,))
    g.result(0, 34)
    g.call(1, 329, (release,), 5000)
    g.result(0, 35)
    g.call(2, 624, 555)
    g.call(2, 694, (slot,))
    g.result(0, 36)
    g.call(2, 300)
    g.result(0, 37)
    g.call(2, 695, (slot,), 0xAABBCCDD)
    g.result(1, 38)
    g.call(1, 234, 0, 0)
    second = 0x10000 + len(g.code)
    g.call(2, 300)
    g.result(0, 40)
    g.call(2, 694, (slot,))
    g.result(0, 41)
    g.call(2, 695, (slot,), 0x12345678)
    g.result(1, 42)
    g.call(1, 234, 0, 0)
    for sentinel, address in ((marker, worker), (second_marker, second)):
        position = g.code.index(struct.pack("<I", sentinel))
        struct.pack_into("<I", g.code, position, address)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "TLSOPEN32", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-open32-tls-") as temporary:
        work = Path(temporary)
        guest = work / "tls.exe"
        guest.write_bytes(program())
        env = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            LD_LIBRARY_PATH=str(build),
            TWOINE_IPC_DIR=str(work / "ipc"),
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
                f"Open32 TLS exit={result.returncode}\n" + result.stderr.decode(errors="replace")
            )
    print(
        "Open32 TLS guest bounds, opaque values,64slots, zeroedreuse and independentworkers passed"
    )


if __name__ == "__main__":
    main()
