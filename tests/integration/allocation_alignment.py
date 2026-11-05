#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Check OS/2 object-base alignment independently of page-granular lengths."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def program(shared=False, tiled=False):
    g = FlatGuest()
    slots = []
    flags = 19 | (0x100 if shared else 0) | (0x40 if tiled else 0)
    for index, size in enumerate((1, 4095, 4096, 4097, 65535, 65536, 65537)):
        slot, length, info = (g.field(bytes(4)) for _ in range(3))
        slots.append((slot, size, index + 0x51))
        if shared:
            g.call(1, 300, slot, 0, size, flags)
        else:
            g.call(1, 299, slot, size, flags)
        g.result(0, 10 + index)
        g.load(slot)
        g.require(b"\xa9\xff\xff\x00\x00", 20 + index)
        # Touch both ends. Recheck all allocations later to detect overlaps.
        g.code.extend(b"\xc6\x00" + bytes([index + 0x51]))
        g.code.extend(b"\xc6\x80" + struct.pack("<I", size - 1) + bytes([index + 0x51]))
        rounded = (size + 4095) & ~4095
        g.code.extend(b"\xc7\x05" + struct.pack("<II", length, 0x100000))
        g.call(1, 306, (slot,), length, info)
        g.result(0, 30 + index)
        g.word(length, rounded, 40 + index)
    for index, (slot, size, byte) in enumerate(slots):
        g.load(slot)
        g.require(b"\x80\x38" + bytes([byte]), 50 + index)
        g.require(b"\x80\xb8" + struct.pack("<I", size - 1) + bytes([byte]), 60 + index)
        g.call(1, 304, (slot,))
        g.result(0, 70 + index)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "OBJALIGN", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-objalign-") as temporary:
        work = Path(temporary)
        env = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            LD_LIBRARY_PATH=str(build),
            TWOINE_IPC_DIR=str(work / "ipc"),
        )
        for shared, tiled in ((False, False), (False, True), (True, False)):
            guest = work / f"allocation-{int(shared)}-{int(tiled)}.exe"
            guest.write_bytes(program(shared, tiled))
            result = subprocess.run(
                [str(build / "lx_loader"), str(guest)],
                cwd=work,
                env=env,
                capture_output=True,
                timeout=20,
            )
            if result.returncode:
                raise RuntimeError(
                    f"shared={shared} tiled={tiled} exit={result.returncode}\n"
                    + result.stderr.decode(errors="replace")
                )
    print("Private/shared 64KiB bases, page-sized lengths, writes, isolation and frees passed")


if __name__ == "__main__":
    main()
