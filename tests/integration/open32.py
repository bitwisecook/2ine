#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise Open32 callbacks, modules, numeric resources and checked PM services."""

import argparse
import json
import os
import struct
import subprocess
import tempfile
from datetime import UTC, datetime, timedelta
from pathlib import Path
from zoneinfo import ZoneInfo

from images import FlatGuest, linear_image
from open32_contracts import (
    api_window_program,
    bitmap_program,
    caret_program,
    critical_program,
    dib_program,
    directory_program,
    drawing_program,
    file_program,
    filetime_program,
    find_program,
    font_program,
    format_program,
    keyboard_program,
    locale_program,
    memory_program,
    menu_program,
    message_program,
    palette_program,
    rectangle_program,
    region_program,
    repaint_program,
    string_resource_program,
    stroke_program,
    thread_program,
    time_program,
)


def program():
    g = FlatGuest()
    handle, other, pointer = (g.field(bytes(4)) for _ in range(3))
    output, needed = g.field(bytes(300)), g.field(bytes(4))
    name, path, text = g.field(b"OPENLIB\0"), g.field(b".\\openlib.dll\0"), g.field(b"hello\0")
    g.call(2, 624, 99)
    g.call(2, 300)
    g.result(99, 10)
    g.call(2, 319, 0)
    g.code.extend(b"\xa3" + struct.pack("<I", handle))
    g.call(2, 318, 0xFFFFFFFF, output, 300)
    g.require(b"\x85\xc0\x0f\x94\xc0\x0f\xb6\xc0\x3d\x00\x00\x00\x00", 11)
    g.require(b"\x66\x81\x3d" + struct.pack("<IH", output, 0x3A43), 12)
    g.call(2, 461, name)
    g.code.extend(b"\xa3" + struct.pack("<I", handle))
    g.call(2, 461, path)
    g.code.extend(b"\xa3" + struct.pack("<I", other))
    g.require(b"\x3b\x05" + struct.pack("<I", handle), 13)
    g.call(2, 341, (handle,), 1)
    g.code.extend(b"\xff\xd0")
    g.result(42, 14)
    g.call(2, 223, (handle,))
    g.result(1, 15)
    g.call(2, 341, (other,), 1)
    g.code.extend(b"\xff\xd0")
    g.result(42, 16)
    g.call(2, 223, (other,))
    g.result(1, 17)
    g.call(2, 341, (other,), 1)
    g.result(0, 18)
    g.call(2, 300)
    g.result(6, 19)
    g.call(2, 213, 0, 7, 10)
    g.result(0x90007, 20)
    g.call(2, 672, 0, 0x90007)
    g.result(4, 21)
    g.call(2, 5007, 0, 0x90007)
    g.code.extend(b"\xa3" + struct.pack("<I", pointer))
    g.require(b"\x81\x38data", 22)
    g.call(2, 476, (pointer,))
    g.require(b"\x3b\x05" + struct.pack("<I", pointer), 23)
    g.call(2, 213, 0, 99, 10)
    g.result(0, 24)
    g.call(2, 300)
    g.result(1814, 25)
    g.call(3, 33, text)
    g.code.extend(b"\xa3" + struct.pack("<I", handle))
    g.call(3, 50, (handle,))
    g.result(5, 26)
    g.call(3, 49, (handle,), 3, output)
    g.result(2, 27)
    g.load(output)
    g.require(b"\x25\xff\xff\xff\x00\x3d\x68\x65\x00\x00", 28)
    g.call(3, 37, (handle,))
    g.result(1, 29)
    g.call(3, 49, (handle,), 3, output)
    g.result(0, 30)
    g.call(3, 37, (handle,))
    g.result(0, 31)
    g.call(4, 397, 0, 0, 3, output, 300, needed)
    g.result(0x4009, 32)
    g.word(needed, 0, 33)
    g.call(5, 250, 0xDEADBEEF, text)
    g.result(0, 34)
    upper = g.field(b"MiXeD\0")
    lower = g.field(b"mixed\0")
    g.call(2, 395, upper)
    g.code.extend(b"\xa3" + struct.pack("<I", handle))
    g.call(2, 395, lower)
    g.require(b"\x3b\x05" + struct.pack("<I", handle), 45)
    g.call(2, 402, (handle,), output, 300)
    g.result(5, 46)
    g.word(output, int.from_bytes(b"MiXe", "little"), 47)
    g.call(2, 397, (handle,))
    g.result(0, 48)
    g.call(2, 399, lower)
    g.require(b"\x3b\x05" + struct.pack("<I", handle), 49)
    g.call(2, 397, (handle,))
    g.result(0, 50)
    g.call(2, 399, lower)
    g.result(0, 51)
    g.call(2, 395, 37)
    g.result(37, 52)
    g.call(2, 402, 37, output, 300)
    g.result(3, 53)
    g.word(output, int.from_bytes(b"#37\0", "little"), 54)
    g.call(2, 6000, 9, 0xDEADBEEF, 0x1122CCDD, 7)
    g.result(123, 35)
    g.exit(0)
    callback = 0x10000 + len(g.code)
    for offset, value, status in ((4, 0xFFFFFFFF, 36), (8, 0, 37), (16, 7, 38)):
        g.require(b"\x81\x7c\x24" + bytes([offset]) + struct.pack("<I", value), status)
    g.code.extend(b"\x8b\x44\x24\x0c")
    g.require(b"\x81\x38alph", 39)
    g.require(b"\x81\x78\x04a be", 40)
    g.require(b"\x66\x81\x78\x08ta", 41)
    g.require(b"\x80\x78\x0a\x00", 42)
    g.code.extend(b"\xb8\x7b\x00\x00\x00\xc3")
    g.code = g.code.replace(struct.pack("<I", 0x1122CCDD), struct.pack("<I", callback))
    return linear_image(
        g.code,
        g.fixups,
        ["DOSCALLS", "PMWINX", "PMDRAG", "PMSPL", "PMWP"],
        "OPEN32",
        data=g.data,
        resources=[(9, 7, b"data")],
    )


def window_program():
    g = FlatGuest()
    name = g.field(b"ContractClass\0")
    title = g.field(b"Open32 lifecycle\0")
    handle, atom, destroyed = (g.field(bytes(4)) for _ in range(3))
    info = g.field(bytes(40))
    record = g.field(struct.pack("<10I", 3, 0x88776655, 8, 4, 0xFFFFFFFF, 0, 0, 0, 0, name))
    g.call(2, 556, record)
    g.code.extend(b"\xa3" + struct.pack("<I", atom))
    g.call(2, 556, record)
    g.result(0, 100)
    g.call(2, 300)
    g.result(1410, 101)
    g.call(2, 240, 0xFFFFFFFF, (atom,), info)
    g.result(1, 102)
    g.word(info, 3, 103)
    g.word(info + 12, 4, 104)
    g.call(2, 99, 0, (atom,), title, 0, 10, 20, 100, 50, 0, 0, 0xFFFFFFFF, 0xDEADC0DE)
    g.require(b"\x3b\x05" + struct.pack("<I", handle), 105)
    g.call(2, 385, (handle,), 0)
    g.result(0xAABBCCDD, 106)
    g.call(2, 705, (atom,), 0xFFFFFFFF)
    g.result(0, 107)
    g.call(2, 300)
    g.result(1412, 108)
    g.call(2, 146, (handle,))
    g.result(1, 109)
    g.word(destroyed, 1, 110)
    g.call(2, 705, (atom,), 0xFFFFFFFF)
    g.result(1, 111)
    g.call(2, 240, 0xFFFFFFFF, name, info)
    g.result(0, 112)
    g.exit(0)
    callback = 0x10000 + len(g.code)
    g.code.extend(b"\x8b\x44\x24\x04\xa3" + struct.pack("<I", handle))
    # Windows WM_NCCREATE must precede WM_CREATE and receive CREATESTRUCT32.
    g.code.extend(b"\x83\x7c\x24\x08\x01\x75")
    branch = len(g.code)
    g.code.extend(b"\0")
    g.code.extend(b"\x8b\x44\x24\x10")
    g.require(b"\x81\x38" + struct.pack("<I", 0xDEADC0DE), 113)
    g.call(2, 659, (handle,), 0, 0xAABBCCDD)
    g.result(0, 114)
    g.code.extend(b"\x31\xc0\xc3")
    g.code[branch] = len(g.code) - branch - 1
    g.code.extend(b"\x83\x7c\x24\x08\x02\x75\x0a\xc7\x05")
    g.code.extend(struct.pack("<II", destroyed, 1))
    g.code.extend(b"\xb8\x01\0\0\0\xc3")
    g.data = g.data.replace(struct.pack("<I", 0x88776655), struct.pack("<I", callback))
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "OPENWINDOW", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-open32-") as temporary:
        work = Path(temporary).resolve()
        env = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            LD_LIBRARY_PATH=str(build),
            SDL_VIDEO_DRIVER="dummy",
            TZ="Pacific/Honolulu",
            TWOINE_USER_PROFILE=str(work / "user.profile"),
            TWOINE_SYSTEM_PROFILE=str(work / "system.profile"),
            TWOINE_REGISTRY=str(work / "registry.profile"),
        )
        (work / "openlib.dll").write_bytes(
            linear_image(b"\xb8\x2a\0\0\0\xc3", [], [], "OPENLIB", library=True, exports=[0])
        )

        def run(image, arguments=()):
            executable = work / "contract.exe"
            executable.write_bytes(image)
            result = subprocess.run(
                [str(build / "lx_loader"), str(executable), *arguments],
                cwd=work,
                env=env,
                capture_output=True,
                timeout=15,
            )
            if result.returncode:
                raise RuntimeError(
                    f"Open32 exit={result.returncode}\n" + result.stderr.decode(errors="replace")
                )
            return result

        run(program(), ["alpha", "beta"])
        run(window_program())
        run(api_window_program())
        run(keyboard_program())
        run(repaint_program())
        run(caret_program())
        run(message_program())
        run(drawing_program())
        run(dib_program())
        run(bitmap_program())
        run(stroke_program())
        run(palette_program())
        run(region_program())
        run(rectangle_program())
        run(menu_program())
        run(memory_program())
        run(string_resource_program())
        (work / "subdir").mkdir()
        assert run(directory_program()).stdout.decode() == "C:" + str(work / "subdir").replace(
            "/", "\\"
        )
        run(file_program())
        assert (work / "io.txt").read_bytes() == b"he"
        run(format_program())
        run(font_program())
        directory = work / "finders"
        directory.mkdir()
        (directory / "cell1.wk").write_bytes(b"cell one")
        (directory / "cell2.WK").write_bytes(b"cell two")
        (directory / "skip.bin").write_bytes(b"skip")
        (directory / "sub.wk").mkdir()
        with (directory / "size.dat").open("wb") as stream:
            stream.truncate((1 << 32) + 7)
        found = run(find_program(r"finders\*.wk")).stdout
        assert len(found) == 3 * 320
        entries = [found[i : i + 320] for i in range(0, len(found), 320)]
        by_name = {entry[44:304].split(b"\0", 1)[0].decode(): entry for entry in entries}
        assert set(by_name) == {"cell1.wk", "cell2.WK", "sub.wk"}
        assert struct.unpack_from("<I", by_name["sub.wk"])[0] & 0x10
        for filename in ("cell1.wk", "cell2.WK"):
            info = (directory / filename).stat()
            entry = by_name[filename]
            assert struct.unpack_from("<II", entry, 28) == (0, info.st_size)
            assert (
                struct.unpack_from("<Q", entry, 20)[0]
                == (int(info.st_mtime) + 11644473600) * 10000000
            )
        found = run(find_program(r"finders\SIZE.DAT")).stdout
        assert struct.unpack_from("<II", found, 28) == (1, 7)
        run(find_program(r"finders\absent.*", missing=2))
        run(find_program(r"missing-directory\*", missing=3))
        run(critical_program())
        run(thread_program())

        epoch = datetime(1601, 1, 1)
        for fields in (
            (1601, 1, 99, 1, 0, 0, 0, 0),
            (1970, 1, 0, 1, 0, 0, 0, 0),
            (2000, 2, 4, 29, 23, 59, 59, 999),
            (2400, 2, 0, 29, 12, 3, 5, 7),
        ):
            year, month, _, day, hour, minute, second, ms = fields
            difference = datetime(year, month, day, hour, minute, second, ms * 1000) - epoch
            expected = (
                difference.days * 86400 + difference.seconds
            ) * 10000000 + difference.microseconds * 10
            run(filetime_program(fields, expected))
        for fields in (
            (1600, 1, 0, 1, 0, 0, 0, 0),
            (1900, 2, 0, 29, 0, 0, 0, 0),
            (2100, 2, 0, 29, 0, 0, 0, 0),
            (2000, 13, 0, 1, 0, 0, 0, 0),
            (2000, 1, 0, 1, 24, 0, 0, 0),
            (2000, 1, 0, 1, 0, 60, 0, 0),
            (2000, 1, 0, 1, 0, 0, 60, 0),
            (2000, 1, 0, 1, 0, 0, 0, 1000),
            (30828, 1, 0, 1, 0, 0, 0, 0),
        ):
            run(filetime_program(fields, None))
        before = datetime.now(UTC)
        timestamp = run(time_program()).stdout
        after = datetime.now(UTC)
        year, month, weekday, day, hour, minute, second, milliseconds = struct.unpack(
            "<8H", timestamp
        )
        observed = datetime(year, month, day, hour, minute, second, milliseconds * 1000, UTC)
        assert weekday == (observed.weekday() + 1) % 7
        assert before - timedelta(milliseconds=1) <= observed <= after

        before = datetime.now(UTC)
        timestamp = run(time_program(local=True)).stdout
        after = datetime.now(UTC)
        year, month, weekday, day, hour, minute, second, milliseconds = struct.unpack(
            "<8H", timestamp
        )
        observed = datetime(
            year,
            month,
            day,
            hour,
            minute,
            second,
            milliseconds * 1000,
            ZoneInfo("Pacific/Honolulu"),
        )
        assert weekday == (observed.weekday() + 1) % 7
        assert before - timedelta(milliseconds=1) <= observed <= after
        locale_reference = (
            Path(__file__).resolve().parents[2]
            / "research/reference_screenshots/123/locale-contracts.json"
        )
        for kind, text in json.loads(locale_reference.read_text())["values"].items():
            run(locale_program(int(kind), text))
        reference = (
            Path(__file__).resolve().parents[2]
            / "research/reference_screenshots/123/pm-contracts.json"
        )
        stock = json.loads(reference.read_text())
        for i, source in enumerate(stock["codepages"]):
            for j, destination in enumerate(stock["codepages"]):
                expected = bytes.fromhex(stock["rows"][i * 3 + j])
                for start in range(0, 256, 64):
                    g = FlatGuest()
                    for c in range(start, start + 64):
                        g.call(2, 711, 0, source, c, destination)
                        g.result(expected[c], c - start + 1)
                    g.exit(0)
                    run(linear_image(g.code, g.fixups, ["DOSCALLS", "PMWIN"], "CODEPAGE"))
        print("Open32 callbacks/modules/resources, drag strings and absent providers passed")
        print("All 2304 code-page translations match the stock Warp probe")


if __name__ == "__main__":
    main()
