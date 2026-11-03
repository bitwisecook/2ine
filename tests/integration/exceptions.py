#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise OS/2 exception registration, unwind notifications and continuation."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def program():
    g = FlatGuest()
    records = [g.field(struct.pack("<3I", 0, 0x88776655, digit)) for digit in (1, 2, 3)]
    sequence, expected_flags, stack = (g.field(bytes(4)) for _ in range(3))
    for record in records:
        g.call(1, 354, record)
        g.result(0, 1)
    g.call(1, 354, records[1])
    g.result(87, 2)
    g.call(1, 355, records[1])
    g.result(87, 3)
    g.call(1, 357, 0xDEADBEEF, 0x10002, 0)
    g.result(87, 4)
    g.code.extend(b"\x64\xa1\0\0\0\0")
    g.result(records[2], 5)
    for index, record in enumerate(records):
        g.word(record, records[index - 1] if index else 0xFFFFFFFF, 6)
    # The target frame is retained; younger handlers see a partial unwind.
    g.code.extend(b"\xc7\x05" + struct.pack("<II", expected_flags, 2))
    g.code.extend(b"\xbb\x44\x33\x22\x11\xbe\x88\x77\x66\x55\xbf\xcc\xbb\xaa\x99")
    g.code.extend(b"\x89\xe0\xa3" + struct.pack("<I", stack))
    g.call(1, 357, records[0], 0x77665544, 0)
    g.exit(90)  # A successful unwind transfers control instead of returning.
    partial = 0x10000 + len(g.code)
    g.code.extend(b"\x83\xc4\x0c\x89\xe0")
    g.require(b"\x3b\x05" + struct.pack("<I", stack), 7)
    g.word(sequence, 32, 8)
    g.code.extend(b"\x64\xa1\0\0\0\0")
    g.result(records[0], 9)
    g.call(1, 355, records[0])
    g.result(0, 10)
    g.call(1, 355, records[0])
    g.result(87, 11)
    g.call(1, 354, records[0])
    g.result(0, 12)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", expected_flags, 6))
    g.code.extend(b"\xc7\x05" + struct.pack("<II", sequence, 0))
    g.call(1, 357, 0, 0x66554433, 0)
    g.exit(91)
    complete = 0x10000 + len(g.code)
    g.code.extend(b"\x83\xc4\x0c")
    g.word(sequence, 1, 13)
    g.code.extend(b"\x64\xa1\0\0\0\0")
    g.result(0xFFFFFFFF, 14)
    g.exit(0)
    handler = 0x10000 + len(g.code)
    # Inspect the actual OS/2 report and suspended integer context.
    g.code.extend(b"\x8b\x44\x24\x04")
    g.require(b"\x81\x38\x26\0\0\xc0", 15)
    g.code.extend(b"\x8b\x40\x04")
    g.require(b"\x3b\x05" + struct.pack("<I", expected_flags), 16)
    g.code.extend(b"\x8b\x44\x24\x0c")
    for offset, value in ((0, 7), (128, 0x99AABBCC), (132, 0x55667788), (140, 0x11223344)):
        g.require(b"\x81\xb8" + struct.pack("<II", offset, value), 17)
    g.code.extend(b"\x8b\x44\x24\x08\x8b\x50\x08")
    g.code.extend(
        b"\xa1"
        + struct.pack("<I", sequence)
        + b"\x6b\xc0\x0a\x01\xd0\xa3"
        + struct.pack("<I", sequence)
    )
    g.code.extend(b"\x31\xc0\xc3")
    g.data = g.data.replace(struct.pack("<I", 0x88776655), struct.pack("<I", handler))
    for sentinel, target in ((0x77665544, partial), (0x66554433, complete)):
        g.code = g.code.replace(struct.pack("<I", sentinel), struct.pack("<I", target))
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "UNWIND", data=g.data)


def dynamic_termination_library():
    g = FlatGuest()
    name = g.field(b"term-result.bin\0")
    value = g.field(b"TERM")
    handle, action, count = (g.field(bytes(4)) for _ in range(3))
    g.code = bytearray(b"\x8b\x44\x24\x08\x83\xf8\x01\x0f\x85")
    branch = len(g.code)
    g.code.extend(bytes(4))
    g.call(1, 273, name, handle, action, 0, 0, 0x12, 0x41, 0)
    g.result(0, 80)
    g.call_handle(1, 282, handle, value, 4, count)
    g.result(0, 81)
    g.call_handle(1, 257, handle)
    g.result(0, 82)
    struct.pack_into("<i", g.code, branch, len(g.code) - branch - 4)
    g.code.extend(b"\xb8\x01\0\0\0\xc3")
    # The DLL's data object is relocated independently from the executable.
    for offset in range(len(g.code) - 3):
        literal = struct.unpack_from("<I", g.code, offset)[0]
        if literal in (name, value, handle, action, count):
            g.fixups.append(struct.pack("<BBhBH", 7, 0, offset, 2, literal - 0x20000))
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "TERM", library=True, data=g.data)


def dynamic_termination_program():
    g = FlatGuest()
    name, handle = g.field(b"term\0"), g.field(bytes(4))
    g.call(1, 318, 0, 0, name, handle)
    g.result(0, 83)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "DYNTERM", data=g.data)


def selector_program(alias=False):
    g = FlatGuest()
    for target, segment, status in ((1, b"\x8c\xca", 70), (2, b"\x8c\xda", 71)):
        g.code.extend(b"\x66\xb8\0\0")
        g.fixups.append(struct.pack("<BBhB", 0x12 if alias else 2, 0, len(g.code) - 2, target))
        g.require(segment + b"\x66\x39\xd0", status)
        g.code.extend(b"\xeb\x06")
        pointer = 0x10000 + len(g.code)
        g.code.extend(bytes(6))
        g.fixups.append(struct.pack("<BBhBH", 6, 0, len(g.code) - 6, target, 0))
        g.word(pointer, target * 0x10000, status + 2)
        g.require(segment + b"\x66\x3b\x15" + struct.pack("<I", pointer + 4), status + 4)
    g.exit(0)
    image = linear_image(g.code, g.fixups, ["DOSCALLS"], "SELECTORS", data=bytes(4))
    if alias:
        struct.pack_into("<I", image, 64 + 16, 0x10)  # E32NOINTFIX, preferred linear offsets.
    return image


def mixed_selector_program():
    g = FlatGuest()
    # A USE32 far jump enters a USE16 object, which jumps back with a 32-bit offset.
    g.code.extend(b"\x66\xea\0\0\0\0")
    g.fixups.append(struct.pack("<BBhB", 0x12, 0, len(g.code) - 2, 3))
    continuation = 0x10000 + len(g.code)
    g.exit(0)
    stub = b"\x66\xea" + struct.pack("<I", continuation) + bytes(2)
    image = linear_image(
        g.code, g.fixups, ["DOSCALLS"], "MIXEDSEL", data=bytes(4), resources=((1, 1, stub),)
    )
    header = 64
    struct.pack_into("<I", image, header + 16, 0x10)  # E32NOINTFIX
    objects = struct.unpack_from("<I", image, header + 64)[0]
    struct.pack_into("<II", image, header + objects + 2 * 24 + 4, 0x30000, 0x1004)
    page_table, records = struct.unpack_from("<II", image, header + 104)
    record = struct.pack("<BBBBh", 0x32, 0, 1, 1, 6)
    insert = header + records + sum(len(fixup) for fixup in g.fixups)
    image[insert:insert] = record
    # Only the third object's page owns the selector-list relocation.
    struct.pack_into(
        "<I", image, header + page_table + 3 * 4, sum(map(len, g.fixups)) + len(record)
    )
    for field in (112, 120):
        old = struct.unpack_from("<I", image, header + field)[0]
        struct.pack_into("<I", image, header + field, old + len(record))
    data_offset = struct.unpack_from("<I", image, header + 128)[0]
    struct.pack_into("<I", image, header + 128, data_offset + len(record))
    return image


def invalid_selector_program(target):
    g = FlatGuest()
    g.code.extend(bytes(2))
    g.fixups.append(struct.pack("<BBhH", 2, 0x40, len(g.code) - 2, target))
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "BADSELECTOR", data=bytes(4))


def raise_program():
    g = FlatGuest()
    # IBM mixed-width REXX thunks use a fixed GDT TIB selector and import the
    # ring-three flat data selector as an absolute 16-bit immediate value.
    g.code.extend(b"\x0f\xa0\x66\xb8\x0b\x15\x8e\xe0\x64\xa1\0\0\0\0\x0f\xa1")
    g.result(0xFFFFFFFF, 28)
    g.code.extend(b"\x66\xb8\0\0")
    g.fixups.append(struct.pack("<BBhBH", 5, 1, len(g.code) - 2, 1, 295))
    g.require(b"\x8c\xda\x66\x39\xd0", 29)
    report = g.field(struct.pack("<10I", 0xE1234567, 0, 0, 0, 5, 11, 22, 33, 44, 55))
    records = [g.field(struct.pack("<2I", 0, address)) for address in (0x77665544, 0x66554433)]
    sequence, stack = (g.field(bytes(4)) for _ in range(2))
    g.call(1, 356, 0)
    g.result(487, 30)
    for record in records:
        g.call(1, 354, record)
        g.result(0, 31)
    g.code.extend(b"\xbb\x44\x33\x22\x11\xbe\x88\x77\x66\x55")
    g.code.extend(b"\x89\xe0\xa3" + struct.pack("<I", stack))
    g.call(1, 356, report)
    g.exit(32)
    continuation = 0x10000 + len(g.code)
    g.result(0x12345678, 33)
    g.require(b"\x81\xfb\xcc\xbb\xaa\x99", 34)
    g.require(b"\x81\xfe\x88\x77\x66\x55", 35)
    g.code.extend(b"\x89\xe0")
    g.require(b"\x3b\x05" + struct.pack("<I", stack), 36)
    g.word(sequence, 21, 37)
    for record in reversed(records):
        g.call(1, 355, record)
        g.result(0, 38)
    g.exit(0)
    handling = 0x10000 + len(g.code)
    g.code.extend(b"\x8b\x44\x24\x04")
    for offset, value in ((0, 0xE1234567), (4, 0), (16, 5), (36, 55)):
        g.require(b"\x81\xb8" + struct.pack("<II", offset, value), 39)
    g.code.extend(b"\x8b\x54\x24\x0c")
    g.require(b"\x81\xba" + struct.pack("<II", 140, 0x11223344), 40)
    for offset, value in ((136, 0x12345678), (140, 0x99AABBCC), (156, continuation)):
        g.code.extend(b"\xc7\x82" + struct.pack("<II", offset, value))
    g.code.extend(b"\xa1" + struct.pack("<I", stack) + b"\x89\x82\xa8\0\0\0")
    g.code.extend(
        b"\x6b\x05" + struct.pack("<I", sequence) + b"\x0a\x40\xa3" + struct.pack("<I", sequence)
    )
    g.code.extend(b"\xb8\xff\xff\xff\xff\xc3")
    searching = 0x10000 + len(g.code)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", sequence, 2) + b"\x31\xc0\xc3")
    for marker, address in ((0x77665544, handling), (0x66554433, searching)):
        g.data = g.data.replace(struct.pack("<I", marker), struct.pack("<I", address))
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "RAISE", data=g.data)


def nested_raise_program():
    g = FlatGuest()
    outer = g.field(struct.pack("<9I", 0xE1234501, 0, 0, 0, 0, 0, 0, 0, 0))
    inner = g.field(struct.pack("<9I", 0xE1234502, 0, 0, 0, 0, 0, 0, 0, 0))
    record = g.field(struct.pack("<2I", 0, 0x77665544))
    g.call(1, 354, record)
    g.result(0, 60)
    g.call(1, 356, outer)
    g.result(0, 61)
    g.exit(0)
    handler = 0x10000 + len(g.code)
    g.code.extend(b"\x8b\x44\x24\x04\x81\x38\x01\x45\x23\xe1\x0f\x85")
    branch = len(g.code)
    g.code.extend(bytes(4))
    g.call(1, 356, inner)
    g.result(0, 62)
    g.code.extend(b"\xb8\xff\xff\xff\xff\xc3")
    inner_handler = len(g.code)
    struct.pack_into("<i", g.code, branch, inner_handler - branch - 4)
    g.require(b"\x81\x38\x02\x45\x23\xe1", 63)
    g.require(b"\x81\x78\x04\x10\0\0\0", 64)
    g.code.extend(b"\x8b\x40\x08")
    g.require(b"\x81\x38\x01\x45\x23\xe1", 65)
    g.code.extend(b"\xb8\xff\xff\xff\xff\xc3")
    g.data = g.data.replace(struct.pack("<I", 0x77665544), struct.pack("<I", handler))
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "NESTEDRAISE", data=g.data)


def fatal_raise_program(disposition=None, flags=0):
    g = FlatGuest()
    report = g.field(struct.pack("<9I", 0xE1234502, flags, 0, 0, 0, 0, 0, 0, 0))
    if disposition is not None:
        record = g.field(struct.pack("<2I", 0, 0x77665544))
        g.call(1, 354, record)
        g.result(0, 50)
    g.call(1, 356, report)
    g.exit(51)
    if disposition is not None:
        handler = 0x10000 + len(g.code)
        g.code.extend(b"\x8b\x44\x24\x04\x81\x38\x02\x45\x23\xe1\x75\x06")
        g.code.extend(b"\xb8" + struct.pack("<I", disposition) + b"\xc3")
        secondary = 0xC0000024 if disposition == 0xFFFFFFFF else 0xC0000025
        g.require(b"\x81\x38" + struct.pack("<I", secondary), 52)
        g.require(b"\x81\x78\x04\x11\0\0\0", 53)
        g.code.extend(b"\x8b\x40\x08")
        g.require(b"\x81\x38\x02\x45\x23\xe1", 54)
        g.exit(0)
        g.data = g.data.replace(struct.pack("<I", 0x77665544), struct.pack("<I", handler))
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "FATALRAISE", data=g.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="twoine-exceptions-") as temporary:
        work = Path(temporary)
        (work / "term.dll").write_bytes(dynamic_termination_library())
        cases = (
            ("unwind", program(), 0),
            ("dynamic_termination", dynamic_termination_program(), 0),
            ("selectors", selector_program(), 0),
            ("prebound_selectors", selector_program(alias=True), 0),
            ("mixed_selectors", mixed_selector_program(), 0),
            ("invalid_selector_zero", invalid_selector_program(0), 1),
            ("invalid_selector_range", invalid_selector_program(65535), 1),
            ("raise", raise_program(), 0),
            ("nested", nested_raise_program(), 0),
            ("unhandled", fatal_raise_program(), 2),
            ("noncontinuable", fatal_raise_program(0xFFFFFFFF, 1), 0),
            ("disposition", fatal_raise_program(42), 0),
        )
        for name, image, expected in cases:
            executable = work / f"{name}.exe"
            executable.write_bytes(image)
            result = subprocess.run(
                [str(build / "lx_loader"), str(executable)],
                cwd=work,
                env=dict(
                    os.environ,
                    TWOINE_BACKEND=args.backend,
                    TWOINE_LIBPATH=str(build),
                    LD_LIBRARY_PATH=str(build),
                ),
                capture_output=True,
                timeout=10,
            )
            if name == "dynamic_termination" and not result.returncode:
                if (work / "term-result.bin").read_bytes() != b"TERM":
                    raise RuntimeError("Dynamic DLL termination did not retain the guest stack")
            if (
                name.startswith("invalid_selector_")
                and b"invalid internal fixup object" not in result.stderr
            ):
                raise RuntimeError("Invalid selector target did not produce a checked loader error")
            if result.returncode != expected:
                raise RuntimeError(
                    f"{name} exit={result.returncode}, expected={expected}\n"
                    + result.stderr.decode(errors="replace")
                )


if __name__ == "__main__":
    main()
