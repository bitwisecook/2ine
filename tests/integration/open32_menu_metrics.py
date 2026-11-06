#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Check menu-check dimensions against native PM bitmap metadata in a generated LX."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def program():
    guest = FlatGuest()
    packed, bitmap, stack, count = (guest.field(bytes(4)) for _ in range(4))
    header = guest.field(bytes(12))
    guest.code.extend(b"\x89\x25" + struct.pack("<I", stack))
    guest.call(2, 624, 0x12345678)
    guest.call(2, 306)
    guest.code.extend(b"\xa3" + struct.pack("<I", packed))
    guest.call(2, 300)
    guest.result(0x12345678, 1)
    guest.call(3, 760, 1, 6)
    guest.code.extend(b"\xa3" + struct.pack("<I", bitmap))
    guest.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 2)
    guest.call(4, 573, (bitmap,), header)
    guest.result(1, 3)
    guest.call(4, 371, (bitmap,))
    guest.result(1, 4)
    guest.code.extend(b"\x0f\xb7\x05" + struct.pack("<I", header + 4))
    guest.code.extend(b"\x0f\xb7\x15" + struct.pack("<I", header + 6))
    guest.code.extend(b"\xc1\xe2\x10\x09\xd0")
    guest.require(b"\x3b\x05" + struct.pack("<I", packed), 5)
    guest.code.extend(b"\xc7\x05" + struct.pack("<II", count, 50))
    loop = len(guest.code)
    guest.call(2, 306)
    guest.require(b"\x3b\x05" + struct.pack("<I", packed), 6)
    guest.code.extend(b"\xff\x0d" + struct.pack("<I", count))
    guest.code.extend(b"\x0f\x85" + struct.pack("<i", loop - len(guest.code) - 6))
    guest.require(b"\x3b\x25" + struct.pack("<I", stack), 7)
    guest.exit(0)
    return linear_image(
        guest.code,
        guest.fixups,
        ["DOSCALLS", "PMWINX", "PMWIN", "PMGPI"],
        "MENUMETRIC",
        data=guest.data,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="menu-metrics-", dir=args.work_dir.resolve()
    ) as temporary:
        work = Path(temporary)
        image = work / "menu-metrics.exe"
        image.write_bytes(program())
        environment = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            LD_LIBRARY_PATH=str(build),
            SDL_VIDEO_DRIVER="dummy",
            SDL_AUDIO_DRIVER="dummy",
            SDL_RENDER_DRIVER="software",
        )
        result = subprocess.run(
            [str(build / "lx_loader"), str(image)],
            cwd=work,
            env=environment,
            capture_output=True,
            timeout=20,
        )
        if result.returncode:
            raise RuntimeError(
                f"Open32 menu metrics exit={result.returncode}\n"
                + result.stderr.decode(errors="replace")
            )
    print("Open32 menu-check metrics match native bitmap metadata and caller-cleaned ABI")


if __name__ == "__main__":
    main()
