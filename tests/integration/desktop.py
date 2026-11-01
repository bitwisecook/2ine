#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Run generated LX guests through desktop settings, resources and module aliases."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def program(checked):
    g = FlatGuest()
    first, second = g.field(bytes(4)), g.field(bytes(4))
    count, output = g.field(bytes(4)), g.field(bytes(64))
    alias = g.field(b"DOSCALL1\0")
    canonical = g.field(b"DOSCALLS\0")
    missing = g.field(b"ABSENT\0")
    private = g.field(b"desktop.ini\0")
    app, key, text = g.field(b"App\0"), g.field(b"Key\0"), g.field(b"hello\0")
    registry = g.field(b"Software\\DesktopTest\0")
    profile, regkey, disposition = (g.field(bytes(4)) for _ in range(3))
    g.call(1, 318, 0, 0, alias, first)  # DosLoadModule permits NULL/zero error buffer
    g.result(0, 10)
    g.call(1, 318, 0, 0, canonical, second)
    g.result(0, 11)
    g.load(first)
    g.require(b"\x3b\x05" + struct.pack("<I", second), 12)
    g.call(1, 318, 0, 0, missing, second)
    g.result(11, 13)
    g.call_handle(1, 322, first)  # DosFreeModule
    g.result(0, 14)
    g.call_handle(1, 322, first)
    g.result(0, 15)
    g.call(1, 573, 0, 5, 1, count)
    g.result(0, 20)
    bundle = struct.pack("<H", 437) + b"\x06hello\0" + b"\x01\0" * 15
    g.word(count, len(bundle), 21)
    g.call(1, 352, 0, 5, 1, first)
    g.result(0, 22)
    g.load(first)
    g.require(b"\x81\x38\xb5\x01\x06h", 23)
    g.call(1, 352, 0, 5, 1, second)
    g.result(0, 24)
    g.load(first)
    g.require(b"\x3b\x05" + struct.pack("<I", second), 25)
    g.call_handle(1, 353, first)
    g.result(0, 26)
    g.call_handle(1, 353, second)
    g.result(0, 27)
    g.call_handle(1, 353, first)
    g.result(87, 28)
    g.call(1, 573, 0, 5, 999, count)
    g.result(87, 29)
    resource_dll = g.field(b"RESOURCE\0")
    g.call(1, 318, 0, 0, resource_dll, profile)
    g.result(0, 80)
    g.call_handle(1, 352, profile, 10, 20, first)
    g.result(0, 81)
    g.call(4, 464, (profile,), 0, output, 64)
    g.result(5, 85)
    g.word(output, int.from_bytes(b"hell", "little"), 86)
    g.call_handle(1, 322, profile)
    g.result(0, 82)
    g.load(first)
    g.require(b"\x81\x38" + b"data", 83)
    g.call_handle(1, 353, first)
    g.result(0, 84)
    g.call(2, 781, 0, 0, 0, 64, output)  # WinLoadString
    g.result(5, 30)
    g.word(output, int.from_bytes(b"hell", "little"), 31)
    g.call(2, 781, 0, 0, 0, 3, output)
    g.result(2, 32)
    g.require(b"\x66\x81\x3d" + struct.pack("<IH", output, 0x6568), 33)
    g.call(2, 781, 0, 0, 1, 64, output)
    g.result(0, 34)
    # A malformed bundle is contained by its declared resource extent.
    g.call(2, 781, 0, 0, 16, 64, output)
    g.result(0, 35)
    g.call(4, 464, 0xFFFFFFFF, 0xFF000000, output, 3)
    g.result(2, 87)
    g.require(b"\x66\x81\x3d" + struct.pack("<IH", output, 0x6568), 88)
    g.require(b"\x80\x3d" + struct.pack("<I", output + 2) + b"\0", 88)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", output, 0x12345678))
    g.call(4, 464, 0, 0, output, 64)
    g.result(0, 89)
    g.word(output, 0x12345678, 90)
    g.call(3, 102, 0, private)
    g.code.extend(b"\xa3" + struct.pack("<I", profile))
    g.require(b"\x83\xf8\x00\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\x00\x00\x00", 40)
    g.call_handle(3, 116, profile, app, key, text)
    g.result(1, 41)
    g.call_handle(3, 115, profile, app, key, 0, output, 64)
    g.result(5, 42)
    g.word(output, int.from_bytes(b"hell", "little"), 43)
    g.call_handle(3, 103, profile)
    g.result(1, 44)
    g.call(4, 543, 0xFFFFFFEF, registry, 0, 0, 0, 3, 0, regkey, disposition)
    g.result(0, 50)
    g.word(disposition, 1, 51)
    g.call_handle(4, 555, regkey, key, 0, 1, text, 6)
    g.result(0, 52)
    g.call_handle(4, 553, regkey, key, 0, first, 0, count)
    g.result(0, 53)
    g.word(count, 6, 54)
    g.word(first, 1, 55)
    g.call_handle(4, 541, regkey)
    g.result(0, 56)
    g.call(4, 542, 0xFFFFFFEF, registry, regkey)
    g.result(0, 63)
    g.call_handle(4, 541, regkey)
    g.result(0, 64)
    g.call(4, 549, 0xFFFFFFEF, registry, regkey)
    g.result(0, 57)
    g.call(4, 549, (regkey,), 0, second)
    g.result(0, 58)
    g.load(regkey)
    g.require(b"\x3b\x05" + struct.pack("<I", second), 59)
    enum_name, enum_data = g.field(bytes(64)), g.field(bytes(64))
    enum_namesize, enum_datasize = g.field(struct.pack("<I", 64)), g.field(struct.pack("<I", 64))
    g.call(4, 548, (regkey,), 0, enum_name, enum_namesize, 0, first, enum_data, enum_datasize)
    g.result(0, 65)
    g.word(enum_namesize, 3, 66)
    g.word(enum_datasize, 6, 67)
    g.word(enum_name, int.from_bytes(b"key\0", "little"), 68)
    g.word(enum_data, int.from_bytes(b"hell", "little"), 69)
    g.call(4, 548, (regkey,), 1, enum_name, enum_namesize, 0, 0, 0, 0)
    g.result(259, 66)
    if checked:
        g.call(4, 548, (regkey,), 0, enum_name, 0xDEADBEEF, 0, 0, 0, 0)
        g.result(87, 67)
        g.call(4, 545, (regkey,), 0xDEADBEEF)
        g.result(87, 68)
    g.call(4, 545, (regkey,), key)
    g.result(0, 69)
    g.call_handle(4, 541, regkey)
    g.result(0, 60)
    if checked:
        g.call(4, 549, 0xFFFFFFEF, 0xDEADBEEF, second)
        g.result(87, 61)
        g.call(4, 549, 0xFFFFFFEF, registry, 0xDEADBEEF)
        g.result(87, 62)
    # The previous procedure returned by subclassing a built-in frame is guest-callable.
    class_name = g.field(b"BridgeClass\0")
    flags = g.field(bytes(4))
    g.call(2, 763, 0)
    g.result(1, 70)
    dc = g.field(bytes(4))
    g.call(4, 263, 0)
    g.code.extend(b"\xa3" + struct.pack("<I", dc))
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 91)
    for index, expected in ((2, 1), (12, 24), (14, 1), (88, 96), (90, 96)):
        g.call(4, 269, (dc,), index)
        g.result(expected, 92)
    g.call(4, 560, 2, (dc,))
    g.result(0, 93)
    g.call(4, 560, 0, (dc,))
    g.result(1, 94)
    g.call(4, 269, (dc,), 88)
    g.result(0, 95)
    g.call(4, 560, 0, (dc,))
    g.result(0, 96)
    g.call(2, 716, 1, 0)
    g.call(2, 926, 1, class_name, 0x10002, 0, 8)
    g.result(1, 71)
    g.call(2, 908, 1, 0, flags, class_name, 0, 0, 0, 0, first)
    g.code.extend(b"\xa3" + struct.pack("<I", profile))
    g.call_handle(2, 929, profile, 0x10002)
    g.code.extend(b"\xa3" + struct.pack("<I", second))
    g.code.extend(b"\x68\x00\x00\x00\x00" * 3 + b"\xff\x35" + struct.pack("<I", profile))
    g.code.extend(b"\xff\x15" + struct.pack("<I", second) + b"\x83\xc4\x10")
    g.result(0, 72)
    g.call_handle(2, 876, first, 4, 0xFEDCBA98)
    g.result(1, 73)
    g.call_handle(2, 839, first, 4)
    g.result(0xFEDCBA98, 74)
    g.call(2, 929, (profile,), (second,))
    g.result(0x10002, 75)
    g.call_handle(2, 728, profile)
    g.result(1, 76)
    g.call(2, 888, 1)
    g.result(1, 77)
    if checked:
        g.call(1, 352, 0, 5, 1, 0xDEADBEEF)
        g.result(87, 60)
        g.call(2, 781, 0, 0, 0, 64, 0xDEADBEEF)
        g.result(0, 61)
        g.call(3, 116, 0xFFFFFFFF, 0xDEADBEEF, key, text)
        g.result(0, 62)
        g.call(4, 550, 0xFFFFFFEF, 0xDEADBEEF, 0, 1, first)
        g.result(87, 63)
    g.exit(0)
    return linear_image(
        g.code,
        g.fixups,
        ["DOSCALLS", "PMWIN", "PMSHAPI", "PMWINX"],
        "DESKTOP",
        data=g.data,
        resources=[(5, 1, bundle), (5, 2, b"\xb5\x01\xffoops")],
    )


def libraries_program():
    g = FlatGuest()
    module = g.field(bytes(4))
    name, missing = g.field(b"RESOURCE\0"), g.field(b"ABSENT\0")
    g.call(2, 461, name)
    g.code.extend(b"\xa3" + struct.pack("<I", module))
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 10)
    g.call(2, 461, name)
    g.require(b"\x3b\x05" + struct.pack("<I", module), 11)
    for expected in (1, 1, 0):
        g.call(2, 223, (module,))
        g.result(expected, 12)
    g.call(2, 461, missing)
    g.result(0, 13)
    g.call(2, 223, 0)
    g.result(0, 14)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "LIBRARIES", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-desktop-") as temporary:
        work = Path(temporary)
        (work / "resource.dll").write_bytes(
            linear_image(
                b"\xb8\x01\x00\x00\x00\xc3",
                [],
                [],
                "RESOURCE",
                library=True,
                resources=[
                    (10, 20, b"data"),
                    (5, 1, struct.pack("<H", 437) + b"\x06hello\0" + b"\x01\0" * 15),
                ],
            )
        )
        executable = work / "desktop.exe"
        executable.write_bytes(program(args.backend == "unicorn"))
        env = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            LD_LIBRARY_PATH=str(build),
            TWOINE_USER_PROFILE=str(work / "user.profile"),
            TWOINE_SYSTEM_PROFILE=str(work / "system.profile"),
            TWOINE_REGISTRY=str(work / "registry.profile"),
            SDL_VIDEO_DRIVER="dummy",
            SDL_RENDER_DRIVER="software",
        )
        result = subprocess.run(
            [str(build / "lx_loader"), str(executable)],
            cwd=work,
            env=env,
            capture_output=True,
            timeout=15,
        )
        if result.returncode:
            raise RuntimeError(
                f"desktop exit={result.returncode}\n" + result.stderr.decode(errors="replace")
            )
        library_guest = work / "libraries.exe"
        library_guest.write_bytes(libraries_program())
        result = subprocess.run(
            [str(build / "lx_loader"), str(library_guest)],
            cwd=work,
            env=env,
            capture_output=True,
            timeout=15,
        )
        if result.returncode:
            raise RuntimeError(
                f"library exit={result.returncode}\n" + result.stderr.decode(errors="replace")
            )
        for corrupt_table in (False, True):
            bad = bytearray(linear_image(b"\xc3", [], [], "BADRES", resources=[(5, 1, b"data")]))
            if corrupt_table:
                struct.pack_into("<I", bad, 64 + 84, 0xFFFFFFFF)
                message = b"Resource table exceeds module bounds"
            else:
                table = struct.unpack_from("<I", bad, 64 + 80)[0]
                struct.pack_into("<H", bad, 64 + table + 8, 4)
                message = b"Resource extent exceeds module object"
            executable.write_bytes(bad)
            result = subprocess.run(
                [str(build / "lx_loader"), str(executable)],
                cwd=work,
                env=env,
                capture_output=True,
                timeout=15,
            )
            if result.returncode != 1 or message not in result.stderr:
                raise RuntimeError(
                    f"invalid resource exit={result.returncode}\n"
                    + result.stderr.decode(errors="replace")
                )
        print(
            "LX resource placement/references, string bundles, module aliases and desktop bridges passed"
        )


if __name__ == "__main__":
    main()
