# Public domain. No rights reserved. Wholly AI-authored.
"""Call real LX GpiDrawBits/QueryBitmapBits exports with bounded 32-bit buffers."""

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
    dc = g.field(bytes(4))
    ps = g.field(bytes(4))
    bitmap = g.field(bytes(4))
    size = g.field(struct.pack("<ii", 2, 2))
    info = bytearray(72)
    struct.pack_into("<IIIHH", info, 0, 64, 2, 2, 1, 1)
    info[68:71] = b"\xff\xff\xff"
    information = g.field(info)
    bits = g.field(b"\x40\0\0\0\x80\0\0\0")
    points = g.field(struct.pack("<8i", 0, 0, 2, 2, 0, 0, 2, 2))
    query = bytearray(64)
    struct.pack_into("<I", query, 0, 64)
    struct.pack_into("<HH", query, 12, 1, 24)
    query_address = g.field(query)
    output = g.field(b"HEAD" + b"\xa5" * 16 + b"TAIL") + 4

    def save(address):
        g.code.extend(b"\xa3" + struct.pack("<I", address))

    g.call(2, 763, 0)
    g.result(1, 10)
    g.call(2, 716, 1, 0)
    g.call(3, 610, 1, 8, g.field(b"*\0"), 0, 0, 0)
    save(dc)
    g.call(3, 369, 1, (dc,), size, 0x5008)
    save(ps)
    g.call(3, 598, (ps,), information, 4, bits, information)
    save(bitmap)
    g.call(3, 506, (ps,), (bitmap,))
    g.result(0, 11)
    g.call(3, 603, (ps,), bits, information, 4, points, 0xCC, 2)
    g.result(1, 12)
    g.call(3, 599, (ps,), 0, 2, output, query_address)
    g.result(2, 13)
    g.word(query_address + 4, 2, 14)
    g.word(query_address + 8, 2, 15)
    g.word(output, 0xFF000000, 16)
    g.word(output + 4, 0x0000FFFF, 17)
    g.word(output + 8, 0x00FFFFFF, 18)
    g.word(output + 12, 0, 19)
    g.word(output - 4, int.from_bytes(b"HEAD", "little"), 20)
    g.word(output + 16, int.from_bytes(b"TAIL", "little"), 21)
    g.call(3, 603, (ps,), 0xFFFFFFF0, information, 4, points, 0xCC, 2)
    g.result(0, 22)
    g.call(3, 603, (ps,), bits, information, 4, 0xFFFFFFF0, 0xCC, 2)
    g.result(0, 23)
    g.call(3, 599, (ps,), 0, 2, 0xFFFFFFF0, query_address)
    g.result(0xFFFFFFFF, 24)
    g.call(3, 599, (ps,), 0, 2, output, 0xFFFFFFF0)
    g.result(0xFFFFFFFF, 25)
    g.word(output - 4, int.from_bytes(b"HEAD", "little"), 26)
    g.word(output + 16, int.from_bytes(b"TAIL", "little"), 27)
    g.call(3, 506, (ps,), 0)
    g.call(3, 371, (bitmap,))
    g.call(3, 379, (ps,))
    g.call(3, 609, (dc,))
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWIN", "PMGPI"], "BITMAPBITS", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=args.work.resolve()))
    exe = work / "BITMAP.EXE"
    exe.write_bytes(program())
    env = {
        k: v
        for k, v in os.environ.items()
        if not k.startswith(("TWOINE_", "TRACE_", "DYLD_", "LD_"))
    }
    env.update(
        TWOINE_BACKEND=args.backend,
        TWOINE_DRIVE_C=str(work),
        TWOINE_LIBPATH=str(build),
        SDL_VIDEO_DRIVER="dummy",
        SDL_AUDIO_DRIVER="dummy",
        SDL_RENDER_DRIVER="software",
    )
    p = subprocess.run(
        [str(build / "lx_loader"), str(exe)], cwd=work, env=env, capture_output=True, timeout=20
    )
    (work / "result.log").write_bytes(p.stdout + p.stderr)
    assert p.returncode == 0, (p.returncode, p.stderr.decode(errors="replace"))
    print(
        "LX GpiDrawBits/QueryBitmapBits pixel bytes, header outputs, pointer bounds and canaries passed"
    )


if __name__ == "__main__":
    main()
