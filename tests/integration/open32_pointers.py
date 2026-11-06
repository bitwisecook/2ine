#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Check the Open32 cursor query ordinal, checked POINT wire and PM pixel mapping."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def program():
    guest = FlatGuest()
    output = guest.field(bytes([0xA5]) * 10)
    native = guest.field(bytes(8))
    height = guest.field(bytes(4))
    stack = guest.field(bytes(4))
    set_result = guest.field(bytes(4))
    guest.code.extend(b"\x89\x25" + struct.pack("<I", stack))
    for pointer in (0, 0xFFFFFFFC):
        guest.call(2, 262, pointer)
        guest.result(0, 1)
        guest.call(2, 300)
        guest.result(87, 2)
    guest.call(2, 624, 0x12345678)
    guest.call(2, 262, output + 1)
    guest.result(1, 3)
    guest.call(2, 300)
    guest.result(0x12345678, 4)
    guest.require(b"\x80\x3d" + struct.pack("<I", output) + b"\xa5", 5)
    guest.require(b"\x80\x3d" + struct.pack("<I", output + 9) + b"\xa5", 6)
    guest.call(3, 823, 1, native)
    guest.result(1, 7)
    guest.call(3, 829, 1, 21)
    guest.code.extend(b"\xa3" + struct.pack("<I", height))
    guest.load(native)
    guest.require(b"\x3b\x05" + struct.pack("<I", output + 1), 8)
    guest.load(height)
    guest.code.extend(b"\x48\x2b\x05" + struct.pack("<I", native + 4))
    guest.require(b"\x3b\x05" + struct.pack("<I", output + 5), 9)
    guest.call(2, 606, 0, 0x80000000)
    guest.result(0, 11)
    guest.call(2, 300)
    guest.result(87, 12)
    guest.call(2, 624, 0x12345678)
    # Both calls target only the current dummy-driver position, never a desktop warp.
    guest.call(2, 606, (output + 1,), (output + 5,))
    guest.code.extend(b"\xa3" + struct.pack("<I", set_result))
    guest.require(b"\x83\xf8\x01\x0f\x96\xc0\x3c\x01", 13)
    guest.call(2, 300)
    guest.code.extend(b"\x83\x3d" + struct.pack("<I", set_result) + b"\x00\x0f\x84" + bytes(4))
    failed_jump = len(guest.code) - 4
    guest.result(0x12345678, 14)
    guest.code.extend(b"\xe9" + bytes(4))
    done_jump = len(guest.code) - 4
    struct.pack_into("<i", guest.code, failed_jump, len(guest.code) - failed_jump - 4)
    guest.result(31, 15)
    struct.pack_into("<i", guest.code, done_jump, len(guest.code) - done_jump - 4)
    guest.call(3, 867, 1, (native,), (native + 4,))
    guest.require(b"\x3b\x05" + struct.pack("<I", set_result), 16)
    guest.require(b"\x3b\x25" + struct.pack("<I", stack), 10)
    guest.exit(0)
    return linear_image(
        guest.code, guest.fixups, ["DOSCALLS", "PMWINX", "PMWIN"], "CURSOR32", data=guest.data
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cursor-", dir=args.work_dir.resolve()) as temporary:
        work = Path(temporary)
        image = work / "cursor.exe"
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
                f"Open32 pointers exit={result.returncode}\n"
                + result.stderr.decode(errors="replace")
            )
    print(
        "Open32 cursor query/set checked POINT, caller cleanup and native PM pixel mapping passed"
    )


if __name__ == "__main__":
    main()
