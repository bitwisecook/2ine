#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise Open32 process-priority ordinals through checked LX ABI bridges."""

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
    guest.call(2, 256)  # GetCurrentProcess
    guest.result(0xFFFFFFFF, 1)
    guest.call(2, 337, 0xFFFFFFFF)  # GetPriorityClass
    guest.result(0x20, 2)
    for handle in (0, 123, 0xFFFFFFFE):
        guest.call(2, 636, handle, 0x20)  # SetPriorityClass
        guest.result(0, 3)
        guest.call(2, 300)  # GetLastError
        guest.result(6, 4)
        guest.call(2, 337, handle)
        guest.result(0, 5)
        guest.call(2, 300)
        guest.result(6, 6)
    for priority in (0, 1, 0x60, 0x8000, 0xFFFFFFFF):
        guest.call(2, 636, 0xFFFFFFFF, priority)
        guest.result(0, 7)
        guest.call(2, 300)
        guest.result(307, 8)
    guest.call(2, 624, 0x1234)  # SetLastError
    for priority in (0x20, 0x80):
        guest.call(2, 636, 0xFFFFFFFF, priority)
        guest.result(1, 9)
        guest.call(2, 300)
        guest.result(0x1234, 10)
        guest.call(2, 337, 0xFFFFFFFF)
        guest.result(priority, 11)
        guest.call(2, 300)
        guest.result(0x1234, 12)
    guest.call(2, 636, 0xFFFFFFFF, 0x100)
    guest.result(0, 13)
    guest.call(2, 300)
    guest.result(50, 14)
    guest.call(2, 337, 0xFFFFFFFF)
    guest.result(0x80, 15)
    guest.require(b"\x3b\x25" + struct.pack("<I", stack), 16)
    guest.exit(0)
    return linear_image(
        guest.code, guest.fixups, ["DOSCALLS", "PMWINX"], "PRIORITY", data=guest.data
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="priority-", dir=args.work.resolve()) as temporary:
        work = Path(temporary)
        executable = work / "priority.exe"
        executable.write_bytes(program())
        environment = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            LD_LIBRARY_PATH=str(build),
            SDL_VIDEO_DRIVER="dummy",
            SDL_RENDER_DRIVER="software",
        )
        result = subprocess.run(
            [str(build / "lx_loader"), str(executable)],
            cwd=work,
            env=environment,
            capture_output=True,
            timeout=20,
        )
        if result.returncode:
            raise RuntimeError(
                f"Open32 priority exit={result.returncode}\n"
                + result.stderr.decode(errors="replace")
            )
    print("Open32 priority LX ABI, validation, real normal scheduling and class/error state passed")


if __name__ == "__main__":
    main()
