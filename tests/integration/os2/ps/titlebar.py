#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""A guest titlebar subclass stores userdata and calls its prior native procedure."""

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
    anchor, queue, title, previous, observed, painted = (guest.field(bytes(4)) for _ in range(6))
    caption = guest.field(b"Guest titlebar subclass\0")
    guest.code = bytearray(b"\xe9\0\0\0\0")
    callback = 0x10000 + len(guest.code)
    guest.code.extend(b"\x55\x8b\xec\x83\x7d\x0c\x23\x0f\x85")
    branch = len(guest.code)
    guest.code.extend(bytes(4))
    guest.code.extend(b"\x6a\0\xff\x75\x08")
    guest.call(2, 843)
    guest.code.extend(b"\x83\xc4\x08\xa3" + struct.pack("<I", observed))
    guest.code.extend(b"\xff\x05" + struct.pack("<I", painted))
    struct.pack_into("<i", guest.code, branch, len(guest.code) - branch - 4)
    guest.code.extend(b"\xff\x75\x14\xff\x75\x10\xff\x75\x0c\xff\x75\x08")
    guest.code.extend(b"\xff\x15" + struct.pack("<I", previous))
    guest.code.extend(b"\x83\xc4\x10\xc9\xc3")
    struct.pack_into("<i", guest.code, 1, len(guest.code) - 5)
    guest.call(2, 763, 0)
    guest.code.extend(b"\xa3" + struct.pack("<I", anchor))
    guest.call(2, 716, (anchor,), 0)
    guest.code.extend(b"\xa3" + struct.pack("<I", queue))
    guest.call(2, 909, 1, 0xFFFF0009, caption, 0x80000000, 0, 0, 200, 24, 0, 3, 1, 0, 0)
    guest.code.extend(b"\xa3" + struct.pack("<I", title))
    guest.call(2, 878, (title,), 0, 0x12345678)
    guest.result(1, 10)
    guest.call(2, 929, (title,), callback)
    guest.code.extend(b"\xa3" + struct.pack("<I", previous))
    # Native prior-procedure pointers must be real executable guest bridges.
    guest.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 11)
    guest.call(2, 920, (title,), 0x23, 0, 0)
    guest.word(observed, 0x12345678, 12)
    guest.word(painted, 1, 13)
    guest.call(2, 728, (title,))
    guest.result(1, 14)
    guest.call(2, 726, (queue,))
    guest.result(1, 15)
    guest.call(2, 888, (anchor,))
    guest.result(1, 16)
    guest.exit(0)
    return linear_image(
        guest.code, guest.fixups, ["DOSCALLS", "PMWIN"], "TITLEBAR", data=guest.data
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--backend", required=True, choices=("native", "unicorn"))
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="2ine-titlebar-") as temporary:
        work = Path(temporary)
        executable = work / "TITLEBAR.EXE"
        executable.write_bytes(program())
        environment = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            SDL_VIDEO_DRIVER="dummy",
            SDL_RENDER_DRIVER="software",
        )
        result = subprocess.run(
            [str(build / "lx_loader"), str(executable)],
            cwd=work,
            env=environment,
            capture_output=True,
            timeout=15,
        )
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
    print("Guest titlebar userdata and prior native procedure bridge passed")


if __name__ == "__main__":
    main()
