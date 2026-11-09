#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Checked LX/Open32 clipboard transfers use a forced dummy video clipboard."""

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
    g = FlatGuest()
    text = g.field(b"caf\xe9\0")
    rich_bytes = b"{\\rtf1\\bin3 a\0b}"
    rich = g.field(rich_bytes)
    name = g.field(b"Rich Text Format\0")
    out_name = g.field(bytes(64))
    text_handle, rich_handle, rich_format, hab = (g.field(bytes(4)) for _ in range(4))
    g.call(3, 763, 0)
    g.code.extend(b"\xa3" + struct.pack("<I", hab))
    g.call(2, 557, name)
    g.code.extend(b"\xa3" + struct.pack("<I", rich_format))
    g.call(3, 744, 1, name)
    g.require(b"\x3b\x05" + struct.pack("<I", rich_format), 10)
    g.call(2, 249, (rich_format,), out_name, 64)
    g.result(16, 11)
    g.word(out_name, int.from_bytes(b"Rich", "little"), 12)
    g.call(2, 502, 0)
    g.result(1, 13)
    g.call(2, 165)
    g.result(1, 14)
    for source, length, handle, format in (
        (text, 5, text_handle, 1),
        (rich, len(rich_bytes), rich_handle, (rich_format,)),
    ):
        g.call(2, 396, 0x42, length)
        g.code.extend(b"\xa3" + struct.pack("<I", handle))
        g.call(2, 404, (handle,))
        g.code.extend(b"\xfc\x89\xc7\xbe" + struct.pack("<I", source))
        g.code.extend(b"\xb9" + struct.pack("<I", length) + b"\xf3\xa4")
        g.call(2, 408, (handle,))
        g.call(2, 602, format, (handle,))
        g.require(b"\x3b\x05" + struct.pack("<I", handle), 15)
    # Adding another format cannot invalidate the transferred text HGLOBAL.
    g.call(2, 248, 1)
    g.require(b"\x3b\x05" + struct.pack("<I", text_handle), 16)
    g.call(2, 248, (rich_format,))
    g.require(b"\x3b\x05" + struct.pack("<I", rich_handle), 17)
    g.call(2, 407, (rich_handle,))
    g.result(len(rich_bytes), 18)
    g.call(3, 806, (hab,), (rich_format,))
    g.require(b"\x81\x38" + struct.pack("<I", int.from_bytes(b"{\\rt", "little")), 19)
    g.require(b"\x80\x78\x0d\x00", 20)
    g.call(2, 56)
    g.result(2, 21)
    g.call(2, 177, 0)
    g.result(1, 22)
    g.call(2, 177, 1)
    g.require(b"\x3b\x05" + struct.pack("<I", rich_format), 23)
    g.call(2, 41)
    g.result(1, 24)
    g.call(2, 502, 0)
    g.result(1, 25)
    g.call(2, 165)
    g.result(1, 26)
    g.call(2, 407, (rich_handle,))
    g.result(0, 27)
    g.call(2, 41)
    g.result(1, 28)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX", "PMWIN"], "CLIPXFER", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    environment = dict(os.environ)
    environment.update(
        TWOINE_BACKEND=args.backend,
        TWOINE_LIBPATH=str(build),
        LD_LIBRARY_PATH=str(build),
        DYLD_LIBRARY_PATH=str(build),
        SDL_VIDEO_DRIVER="dummy",
        SDL_RENDER_DRIVER="software",
        SDL_AUDIO_DRIVER="dummy",
    )
    with tempfile.TemporaryDirectory(prefix="twoine-clipboard-") as temporary:
        path = Path(temporary) / "clipboard.exe"
        path.write_bytes(program())
        result = subprocess.run(
            [str(build / "lx_loader"), str(path)],
            cwd=temporary,
            env=environment,
            capture_output=True,
            timeout=15,
        )
        if result.returncode:
            raise RuntimeError(
                f"Clipboard fixture exit={result.returncode}\n"
                + result.stderr.decode(errors="replace")
            )
    print("Guest/Open32 clipboard ownership, shared atom IDs and binary RTF passed")


if __name__ == "__main__":
    main()
