#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Unimplemented guest imports end their worker with status 1, never SIGABRT."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def missing_program(named=False, callback=False):
    g = FlatGuest()
    if callback:
        message = g.field(bytes(28))
        g.call(2, 653, 0, 0, 1, 0x1122CCDD)  # SetTimer
        g.call(2, 311, message, 0, 0x113, 0x113)  # GetMessage
        g.call(2, 149, message)  # DispatchMessage calls the guest TIMERPROC.
        g.exit(99)
        procedure = 0x10000 + len(g.code)
        g.code = g.code.replace(struct.pack("<I", 0x1122CCDD), struct.pack("<I", procedure))
    g.call(2, 65534)
    if named:
        fixup = bytearray(g.fixups[-1])
        fixup[1] = 2  # Imported procedure name, at offset 1 in the name table.
        fixup[-2:] = b"\x01\0"
        g.fixups[-1] = bytes(fixup)
    g.exit(98)
    name = b"TwoineMissingForTest"
    return linear_image(
        g.code,
        g.fixups,
        ["DOSCALLS", "PMWINX"],
        "MISSING",
        data=g.data,
        import_names=b"\0" + bytes([len(name)]) + name,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--backend", required=True, choices=("native", "unicorn"))
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="missing-import-", dir=build) as directory:
        work = Path(directory).resolve()
        env = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            LD_LIBRARY_PATH=str(build),
            DYLD_LIBRARY_PATH=str(build),
            SDL_VIDEO_DRIVER="dummy",
            SDL_AUDIO_DRIVER="dummy",
            TWOINE_IPC_DIR=str(work / "ipc"),
        )
        for named in (False, True):
            for callback in (False, True):
                exe = work / "missing.exe"
                exe.write_bytes(missing_program(named, callback))
                result = subprocess.run(
                    [str(build / "lx_loader"), str(exe)],
                    cwd=work,
                    env=env,
                    capture_output=True,
                    timeout=10,
                )
                expected = b"TwoineMissingForTest" if named else b"ordinal #65534"
                assert result.returncode == 1, (result.returncode, result.stderr)
                assert expected in result.stderr and b"PMWINX" in result.stderr, result.stderr
                # The controlling process and future OS/2 sessions remain usable.
                good = FlatGuest()
                good.exit(37)
                exe.write_bytes(linear_image(good.code, good.fixups, ["DOSCALLS"], "SURVIVE"))
                result = subprocess.run(
                    [str(build / "lx_loader"), str(exe)],
                    cwd=work,
                    env=env,
                    capture_output=True,
                    timeout=10,
                )
                assert result.returncode == 37, (result.returncode, result.stderr)
    print("Missing ordinal/name imports exit 1 from entry and nested GUI callbacks; next sessions run")


if __name__ == "__main__":
    main()
