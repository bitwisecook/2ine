#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Verify mapped guest paths and isolated profiles/registry with independent LX processes."""

import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image

ROOT = Path(__file__).resolve().parents[2]


def program(previous):
    g = FlatGuest()
    drive, bitmap, module = (g.field(bytes(4)) for _ in range(3))
    output = g.field(bytes(128))
    name = g.field(b"BALLPROBE\0")
    path = g.field(b"C:\\OS2\\probe.exe\0")
    g.call(1, 275, drive, bitmap)
    g.result(0, 10)
    g.word(drive, 3, 11)
    g.word(bitmap, (1 << 2) | (1 << 3), 12)
    g.call(1, 319, name, module)
    g.result(0, 13)
    g.call_handle(1, 320, module, 128, output)
    g.result(0, 14)
    expected_path = b"C:\\OS2\\probe.exe\0"
    for i in range(0, len(expected_path) - 3, 4):
        g.word(output + i, int.from_bytes(expected_path[i : i + 4], "little"), 15 + i)
    g.call(1, 223, path, 5, output, 128)
    g.result(0, 40)
    g.word(output, int.from_bytes(expected_path[:4], "little"), 41)
    directory = g.field(b"C:\\TEMP\0")
    length = g.field(struct.pack("<I", 128))
    g.call(1, 255, directory)
    g.result(0, 42)
    g.call(1, 274, 0, output, length)
    g.result(0, 43)
    g.word(output, int.from_bytes(b"TEMP", "little"), 44)
    existing = g.field(b"C:\\OS2\0")
    created = g.field(b"C:\\OS2\\InstallerDirectory\0")
    missing = g.field(b"C:\\missing-parent\\child\0")
    g.call(1, 270, existing, 0)
    g.result(5, 45)
    g.call(1, 270, created, 0)
    g.result(5 if previous else 0, 46)
    g.call(1, 270, created, 0)
    g.result(5, 47)
    g.call(1, 270, missing, 0)
    g.result(3, 48)
    search = g.field(b"C:\\OS2\\*.msg\0")
    directory_handle = g.field(struct.pack("<I", 0xFFFF))
    found = g.field(struct.pack("<I", 1))
    g.call(1, 264, search, directory_handle, 7, output, 128, found, 1)
    g.result(0, 95)
    g.word(found, 1, 96)
    g.call_handle(1, 263, directory_handle)
    g.result(0, 97)
    source = g.field(b"C:\\OS2\\MESSAGE-1.MSG\0")
    g.call(1, 258, source, created, 1)
    g.result(0, 98)
    g.call(1, 258, source, existing, 1)
    g.result(5, 99)
    app, key = g.field(b"Ball\0"), g.field(b"Value\0")
    fallback, text = g.field(b"none\0"), g.field(b"saved\0")
    g.call(6, 763, 0)
    g.result(1, 90)
    g.call(6, 753, 1)
    absent = g.field(b"Absent\0")
    g.call(2, 115, 0xFFFFFFFF, absent, absent, 0, output, 128)
    g.result(0, 91)
    g.call(6, 753, 1)
    g.result(0x1304, 92)
    g.call(2, 115, 0xFFFFFFFF, app, key, fallback, output, 128)
    g.result(5 if previous else 4, 50)
    g.word(output, int.from_bytes(b"save" if previous else b"none", "little"), 51)
    g.call(2, 116, 0xFFFFFFFF, app, key, text)
    g.result(1, 52)
    registry = g.field(b"Software\\BallProbe\0")
    handle, disposition, size, kind = (g.field(bytes(4)) for _ in range(4))
    g.call(3, 543, 0xFFFFFFEF, registry, 0, 0, 0, 3, 0, handle, disposition)
    g.result(0, 60)
    g.word(disposition, 2 if previous else 1, 61)
    if previous:
        g.code.extend(b"\xc7\x05" + struct.pack("<II", size, 128))
        g.call_handle(3, 553, handle, key, 0, kind, output, size)
        g.result(0, 62)
        g.word(output, int.from_bytes(b"save", "little"), 63)
    g.call_handle(3, 555, handle, key, 0, 1, text, 6)
    g.result(0, 64)
    g.call_handle(3, 541, handle)
    g.result(0, 65)
    identifier = g.field(b"<BALL_APP>\0")
    g.call(5, 252, identifier)
    if previous:
        g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 66)
    else:
        g.result(0, 66)
    class_name = g.field(b"WPProgram\0")
    title = g.field(b"Ball application\0")
    setup = g.field(b"OBJECTID=<BALL_APP>;EXENAME=C:\\OS2\\probe.exe;\0")
    desktop = g.field(b"<WP_DESKTOP>\0")
    g.call(5, 281, class_name, title, setup, desktop, 2)
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 67)
    stack = g.field(bytes(4))
    g.code.extend(b"\x89\x25" + struct.pack("<I", stack))
    for index, version in enumerate((1, 0x200, 0x201)):
        message = g.field(f"C:\\OS2\\message-{version}.msg\0".encode())
        for number, expected, actual in ((154, b"old!", 6), (155, b"end!", 4)):
            g.call(4, 6, 0, 0, 0, output, 128, number, message, size)
            g.code[-1] = 28  # MSG removes its four-byte hidden word.
            g.result(0, 70 + index)
            g.word(output, int.from_bytes(expected, "little"), 74 + index)
            g.word(size, actual, 78 + index)
    g.require(b"\x3b\x25" + struct.pack("<I", stack), 82)
    g.exit(0)
    return linear_image(
        g.code,
        g.fixups,
        ["DOSCALLS", "PMSHAPI", "PMWINX", "MSG", "PMWP", "PMWIN"],
        "BALLPROBE",
        data=g.data,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="twoine-balls-") as temporary:
        root = Path(temporary)
        media = root / "media"
        media.mkdir()
        launcher = [sys.executable, str(ROOT / "tools/balls.py")]
        for name in ("first", "second"):
            ball = root / name
            base = [
                *launcher,
                "--ball",
                str(ball),
                "--build",
                str(args.build.resolve()),
                "--backend",
                args.backend,
            ]
            subprocess.run([*base, "init"], check=True, capture_output=True)
            subprocess.run([*base, "map", "D", str(media)], check=True, capture_output=True)
            for version in (1, 0x200, 0x201):
                width = 2 if version & 1 else 4
                first = 31 + width * 2
                records = (b"Iold!\r\n", b"Iend!")
                header = bytearray(31)
                header[:11] = b"\xffMKMSGF\0TST"
                struct.pack_into("<HHH", header, 11, 2, 154, version)
                if version >= 0x200:
                    struct.pack_into("<H", header, 18, 31)
                offsets = struct.pack(
                    "<HH" if width == 2 else "<II", first, first + len(records[0])
                )
                (ball / f"drive_c/OS2/message-{version}.msg").write_bytes(
                    header + offsets + b"".join(records)
                )
            executable = ball / "drive_c/OS2/probe.exe"
            for previous in (False, True):
                executable.write_bytes(program(previous))
                result = subprocess.run(
                    [*base, "run", r"C:\os2\PROBE.EXE"],
                    capture_output=True,
                    timeout=15,
                    env=dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_RENDER_DRIVER="software"),
                )
                if result.returncode:
                    raise AssertionError(
                        f"{name}/{previous}: {result.returncode}\n{result.stderr.decode(errors='replace')}"
                    )
            for path in ("OS2.INI", "REGISTRY.INI", "OBJECTS.INI"):
                assert (ball / "drive_c/OS2" / path).stat().st_size > 12
            source = ball / "drive_c/OS2/message-1.msg"
            assert (
                ball / "drive_c/OS2/InstallerDirectory/MESSAGE-1.MSG"
            ).read_bytes() == source.read_bytes()
            data = json.loads((ball / "ball.json").read_text())
            assert data["drives"]["C"] == "drive_c"
        print(
            "Two balls retained independent profiles, registry and mapped guest module/full paths"
        )


if __name__ == "__main__":
    main()
