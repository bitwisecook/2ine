#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Execute LX forwarded function/data exports and reject invalid forwarder chains."""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from tests.integration.harness.images import FlatGuest, linear_image


def forward(module, target, by_ordinal=True):
    return struct.pack("<BHI", int(by_ordinal), module, target)


def library(name, modules, entries, names=(), imports=b"\0"):
    return linear_image(
        b"\xb8\x01\x00\x00\x00\xc3\xb8\x2a\x00\x00\x00\xc3",
        [],
        modules,
        name,
        library=True,
        data=struct.pack("<I", 0x12345678),
        entry_table=entries,
        export_names=names,
        import_names=imports,
    )


def program():
    g = FlatGuest()
    for module, ordinal in ((3, 1), (3, 2), (2, 5), (2, 7)):
        g.call(module, ordinal)
        g.result(42, 10 + ordinal)
    g.code.extend(b"\xa1\0\0\0\0")
    g.fixups.append(struct.pack("<BBhBH", 7, 1, len(g.code) - 4, 2, 6))
    g.result(0x12345678, 30)
    # Query the published forwarder by both ordinal and exported name.
    handle, address = g.field(bytes(4)), g.field(bytes(4))
    module_name, entry_name = g.field(b"MID\0"), g.field(b"Answer\0")
    g.call(1, 319, module_name, handle)  # DosQueryModuleHandle
    g.result(0, 31)
    for ordinal, name in ((1, 0), (0, entry_name)):
        g.call(1, 321, (handle,), ordinal, name, address)  # DosQueryProcAddr
        g.result(0, 32)
        g.code.extend(b"\xff\x15" + struct.pack("<I", address))
        g.result(42, 33)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "MID", "THIRD"], "EXPORTS", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    env = dict(
        os.environ,
        TWOINE_BACKEND=args.backend,
        TWOINE_LIBPATH=str(build),
        LD_LIBRARY_PATH=str(build),
        SDL_VIDEO_DRIVER="dummy",
        SDL_AUDIO_DRIVER="dummy",
    )
    with tempfile.TemporaryDirectory(prefix="twoine-exports-") as directory:
        work = Path(directory)

        def run(image, error=None):
            exe = work / "exports.exe"
            exe.write_bytes(image)
            result = subprocess.run(
                [str(build / "lx_loader"), str(exe)],
                cwd=work,
                env=env,
                capture_output=True,
                timeout=10,
            )
            if error is None:
                assert result.returncode == 0, f"exit={result.returncode}\n" + result.stderr.decode(
                    errors="replace"
                )
            else:
                assert result.returncode == 1 and error in result.stderr, result.stderr.decode(
                    errors="replace"
                )

        base = b"\x01\x03\x01\0\x01" + struct.pack("<I", 6)
        base += b"\x01\x03\x02\0\x01" + struct.pack("<I", 0)
        base += b"\x01\x01\x01\0\x01\x06\0"
        (work / "base.dll").write_bytes(library("BASE", [], base, [(1, "Answer")]))
        mid = b"\x02\x04\0\0" + forward(1, 1) + forward(1, 1, False)
        mid += b"\x02\0\x01\x03\x01\0\x01" + struct.pack("<I", 6)
        mid += b"\x02\x04\0\0" + forward(1, 2) + forward(1, 3)
        (work / "mid.dll").write_bytes(
            library("MID", ["BASE"], mid, [(2, "Answer")], b"\0\x06Answer")
        )
        third = b"\x02\x04\0\0" + forward(1, 1) + forward(1, 1, False)
        (work / "third.dll").write_bytes(library("THIRD", ["MID"], third, imports=b"\0\x06Answer"))
        run(program())
        # B resolves A's forwarder while A is still waiting for B to finish loading.
        (work / "a.dll").write_bytes(library("A", ["B"], b"\x01\x04\0\0" + forward(1, 1)))
        (work / "b.dll").write_bytes(
            library(
                "B",
                ["A"],
                b"\x01\x03\x01\0\x01" + struct.pack("<I", 6) + b"\x01\x04\0\0" + forward(1, 1),
            )
        )
        g = FlatGuest()
        g.call(2, 1)
        g.result(42, 40)
        g.call(3, 2)
        g.result(42, 41)
        g.exit(0)
        run(linear_image(g.code, g.fixups, ["DOSCALLS", "A", "B"], "CIRCULAR", data=g.data))
        main_image = linear_image(b"\xc3", [], ["BAD"], "INVALID")
        cases = (
            (forward(2, 1), b"Invalid LX entry table"),
            (forward(1, 0), b"Invalid LX entry table"),
            (forward(1, 0xFFFFFFFF, False), b"Invalid LX entry table"),
            (forward(1, 999), b"Cannot resolve LX forwarder"),
        )
        for entry, error in cases:
            (work / "bad.dll").write_bytes(library("BAD", ["BASE"], b"\x01\x04\0\0" + entry))
            run(main_image, error)
        (work / "bad.dll").write_bytes(library("BAD", ["BAD"], b"\x01\x04\0\0" + forward(1, 1)))
        run(main_image, b"Circular LX forwarder chain")
        (work / "bad.dll").write_bytes(library("BAD", ["OTHER"], b"\x01\x04\0\0" + forward(1, 1)))
        (work / "other.dll").write_bytes(library("OTHER", ["BAD"], b"\x01\x04\0\0" + forward(1, 1)))
        run(main_image, b"Circular LX forwarder chain")
        truncated = library("BAD", [], b"\0")
        struct.pack_into("<I", truncated, 64 + 92, len(truncated) - 65)
        truncated[-1] = 1
        (work / "bad.dll").write_bytes(truncated)
        run(main_image, b"Invalid LX entry table")
        print(
            "LX ordinal/name chains, mixed bundles, data exports, procedure queries and invalid chains passed"
        )


if __name__ == "__main__":
    main()
