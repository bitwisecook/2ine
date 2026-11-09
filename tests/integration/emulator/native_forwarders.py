#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Call native forwarder exports and query them through DosQueryProcAddr."""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from tests.integration.harness.images import FlatGuest, linear_image

DOSCALLS, NATFWD = 1, 2
DOS_QUERY_MODULE_HANDLE, DOS_QUERY_PROC_ADDR = 319, 321


def program():
    g = FlatGuest()
    handle, natfwd_handle = g.field(bytes(4)), g.field(bytes(4))
    direct, forwarded = g.field(bytes(4)), g.field(bytes(4))
    doscalls_name, natfwd_name = g.field(b"DOSCALLS\0"), g.field(b"NATFWD\0")
    # NATFWD.1 forwards to DOSCALLS.319 by ordinal.
    g.call(NATFWD, 1, doscalls_name, handle)
    g.result(0, 11)
    # NATFWD.2 forwards to DOSCALLS by the entry name DosQueryProcAddr.
    g.call(NATFWD, 2, (handle,), DOS_QUERY_MODULE_HANDLE, 0, direct)
    g.result(0, 12)
    g.call(DOSCALLS, DOS_QUERY_MODULE_HANDLE, natfwd_name, natfwd_handle)
    g.result(0, 13)
    g.call(DOSCALLS, DOS_QUERY_PROC_ADDR, (natfwd_handle,), 1, 0, forwarded)
    g.result(0, 14)
    # The queried forwarder is DosQueryModuleHandle: call it through the pointer
    # and compare its DOSCALLS handle with the first one.
    g.code.extend(b"\x68" + struct.pack("<I", natfwd_handle))
    g.code.extend(b"\x68" + struct.pack("<I", doscalls_name))
    g.code.extend(b"\xff\x15" + struct.pack("<I", forwarded) + b"\x83\xc4\x08")
    g.result(0, 15)
    g.load(handle)
    g.require(b"\x3b\x05" + struct.pack("<I", natfwd_handle), 16)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "NATFWD"], "NATIVEFW", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--modules", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    env = dict(
        os.environ,
        TWOINE_BACKEND=args.backend,
        TWOINE_LIBPATH=f"{build}:{args.modules.resolve()}",
        LD_LIBRARY_PATH=str(build),
        SDL_VIDEO_DRIVER="dummy",
        SDL_AUDIO_DRIVER="dummy",
    )
    with tempfile.TemporaryDirectory(prefix="twoine-native-forwarders-") as directory:
        exe = Path(directory) / "natfwd.exe"
        exe.write_bytes(program())
        result = subprocess.run(
            [str(build / "lx_loader"), str(exe)],
            cwd=directory,
            env=env,
            capture_output=True,
            timeout=10,
        )
        assert result.returncode == 0, f"exit={result.returncode}\n" + result.stderr.decode(
            errors="replace"
        )
    print("Native forwarder exports by ordinal and by name resolved to DOSCALLS")


if __name__ == "__main__":
    main()
