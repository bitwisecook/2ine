#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise the flat guest API contracts required by classic REXX support DLLs."""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image
from legacy import Guest as Guest16

HEAD = int.from_bytes(b"HEAD", "little")
TAIL = int.from_bytes(b"TAIL", "little")


class Guest(FlatGuest):
    def store(self, address, value):
        self.code.extend(b"\xc7\x05" + struct.pack("<II", address, value))

    def image(self, name):
        self.exit(0)
        return linear_image(self.code, self.fixups, ["DOSCALLS"], name, data=self.data)

    def guarded(self, size):
        block = self.field(b"HEAD" + b"\xcc" * size + b"TAIL")
        return block + 4

    def guards(self, pointer, size, failure):
        self.word(pointer - 4, HEAD, failure)
        self.word(pointer + size, TAIL, failure + 1)


def mixed_library():
    # Original LX entry bundles describe each export's own object width.
    image = linear_image(b"\xb8\x01\0\0\0\xc3", [], [], "MIXED", library=True, data=b"\xcb")
    header = struct.unpack_from("<I", image, 60)[0]
    objects = struct.unpack_from("<I", image, header + 64)[0]
    struct.pack_into("<I", image, header + objects + 24 + 8, 5)  # USE16 code object.
    struct.pack_into("<II", image, header + 32, 0, 0)  # DLL entry uses the caller's stack.
    struct.pack_into("<I", image, header + 148, 0)  # No automatic data object.
    entries = b"\x01\x03\x01\0\x01\0\0\0\0"  # Ordinal 1, object 1, USE32.
    entries += b"\x01\x01\x02\0\x01\0\0\0\0"  # Ordinal 2, object 2, USE16; terminator.
    pages = struct.unpack_from("<I", image, header + 128)[0]
    offset = pages - len(entries)
    assert offset > header + struct.unpack_from("<I", image, header + 120)[0]
    image[offset:pages] = entries
    struct.pack_into("<I", image, header + 92, offset - header)
    return image


def procedure_types(checked):
    g = Guest()
    module = g.field(bytes(4))
    output = g.guarded(4)
    name = g.field(b"DOSCALLS\0")
    g.call(1, 319, name, module)
    g.result(0, 10)
    for ordinal, symbol, expected in ((47, b"Dos16GetModHandle\0", 0), (282, b"DosWrite\0", 1)):
        for by_name in (False, True):
            g.call(
                1,
                586,
                (module,),
                0 if by_name else ordinal,
                g.field(symbol) if by_name else 0,
                output,
            )
            g.result(0, 11)
            g.word(output, expected, 12)
            g.guards(output, 4, 13)
    for ordinal, symbol, expected in (
        (9999, 0, 182),
        (0, g.field(b"DOES_NOT_EXIST\0"), 123),
        (0, 0, 87),
    ):
        g.store(output, 0xCCCCCCCC)
        g.call(1, 586, (module,), ordinal, symbol, output)
        g.result(expected, 15)
        g.word(output, 0xCCCCCCCC, 16)
    g.call(1, 586, 0xFFFFFFFE, 282, 0, output)
    g.result(6, 17)
    g.call(1, 586, (module,), 282, 0, 0)
    g.result(87, 18)
    if checked:
        g.call(1, 586, (module,), 282, 0, 0x2FFFE)
        g.result(87, 19)
    g.call(1, 318, 0, 0, g.field(b"MIXED\0"), module)
    g.result(0, 20)
    for ordinal, expected in ((1, 1), (2, 0)):
        g.call(1, 586, (module,), ordinal, 0, output)
        g.result(0, 21)
        g.word(output, expected, 22)
    g.call(1, 322, (module,))
    g.result(0, 23)
    return g.image("REXXTYPE")


def names_and_timers(checked):
    g = Guest()
    output = g.guarded(16)
    source = g.field(b"report.txt\0")
    for mask, expected in ((b"*.bak\0", b"report.bak\0"), (b"???.*\0", b"rep.txt\0")):
        g.call(1, 261, 1, source, g.field(mask), output, 16)
        g.result(0, 30)
        for index, value in enumerate(expected):
            g.require(b"\x80\x3d" + struct.pack("<I", output + index) + bytes([value]), 31)
        g.guards(output, 16, 32)
    g.store(output, 0xCCCCCCCC)
    mask = g.field(b"*.bak\0")
    for level, name, capacity, expected in (
        (2, source, 16, 87),
        (1, source, 3, 123),
        (1, source, 0, 87),
        (1, g.field(b"C:\\report.txt\0"), 16, 123),
    ):
        g.call(1, 261, level, name, mask, output, capacity)
        g.result(expected, 34)
        g.word(output, 0xCCCCCCCC, 35)
    frequency = g.guarded(4)
    first, second = g.guarded(8), g.guarded(8)
    g.call(1, 362, frequency)
    g.result(0, 40)
    g.load(frequency)
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 41)  # Nonzero timer frequency.
    g.guards(frequency, 4, 42)
    for pointer in (first, second):
        g.call(1, 363, pointer)
        g.result(0, 44)
        g.guards(pointer, 8, 45)
        g.call(1, 229, 2)  # DosSleep separates the samples without fixing a clock frequency.
        g.result(0, 47)
    g.load(second)
    g.code.extend(b"\x2b\x05" + struct.pack("<I", first))
    g.code.extend(b"\x8b\x15" + struct.pack("<I", second + 4))
    g.code.extend(b"\x1b\x15" + struct.pack("<I", first + 4))
    g.require(b"\x0f\x93\xc0\x3c\x01", 48)  # Unsigned 64-bit second >= first.
    for ordinal in (362, 363):
        g.call(1, ordinal, 0)
        g.result(87, 49)
    if checked:
        g.call(1, 362, 0x2FFFE)
        g.result(87, 50)
        g.call(1, 363, 0x2FFFC)
        g.result(87, 51)
    g.call(1, 415, 1)
    g.result(87, 52)
    g.call(1, 415, 0)
    g.result(0, 53)
    return g.image("REXXNAMES")


def enumeration(checked):
    g = Guest()
    path = g.field(b"attributes.dat\0")
    empty = g.field(b"empty.dat\0")
    handle, action = g.field(bytes(4)), g.field(bytes(4))
    count = g.guarded(4)
    output = g.guarded(32)
    g.call(1, 273, path, handle, action, 0, 0, 1, 0x40, 0)
    g.result(0, 60)
    for reference_type, reference in ((1, path), (0, handle)):
        g.store(count, 5)
        g.call(1, 372, reference_type, reference, 1, output, 32, count, 1)
        g.result(0, 61)
        g.word(count, 2, 62)
        g.word(output, 12, 63)
        g.word(output + 4, 0x00050180, 64)
        g.word(output + 8, 0xCCCC004B, 65)
        g.word(output + 12, 0, 66)
        g.word(output + 16, 0x00020400, 67)
        g.word(output + 20, int.from_bytes(b"LONG", "little"), 68)
        g.word(output + 24, 0xCCCCCC00, 69)
        g.guards(output, 32, 70)
        g.guards(count, 4, 72)
    for capacity, expected, returned in ((9, 111, 0), (10, 0, 1)):
        g.store(count, 5)
        g.store(output, 0xCCCCCCCC)
        g.call(1, 372, 1, path, 1, output, capacity, count, 1)
        g.result(expected, 74)
        g.word(count, returned, 75)
        g.word(output, 0 if returned else 0xCCCCCCCC, 76)
    g.store(count, 5)
    g.call(1, 372, 1, path, 2, output, 32, count, 1)
    g.result(0, 77)
    g.word(count, 1, 78)
    g.word(output, 0, 79)
    g.word(output + 4, 0x00020400, 80)
    for reference, first in ((path, 3), (empty, 1)):
        g.store(count, 5)
        g.call(1, 372, 1, reference, first, output, 32, count, 1)
        g.result(0, 81)
        g.word(count, 0, 82)
    g.store(count, 0)
    g.call(1, 372, 1, path, 1, output, 32, count, 1)
    g.result(0, 83)
    g.word(count, 0, 84)
    for reference_type, first, level, expected in ((2, 1, 1, 87), (1, 0, 1, 87), (1, 1, 2, 124)):
        g.call(1, 372, reference_type, path, first, output, 32, count, level)
        g.result(expected, 85)
    g.call(1, 372, 1, path, 1, output, 32, 0, 1)
    g.result(87, 86)
    if checked:
        g.call(1, 372, 1, 0xDEADBEEF, 1, output, 32, count, 1)
        g.result(87, 87)
        g.call(1, 372, 1, path, 1, 0x2FFF0, 32, count, 1)
        g.result(87, 88)
    g.call(1, 257, (handle,))
    g.result(0, 89)
    return g.image("REXXENUM")


def tiled_memory():
    g = Guest()
    memory = g.field(bytes(4))
    flat = g.field(bytes(4))
    far = g.field(bytes(4))
    extent = g.field(struct.pack("<I", 1000000))
    flags = g.field(bytes(4))
    g.call(1, 299, memory, 70000, 0x53)
    g.result(0, 90)
    g.load(memory)
    g.require(b"\x25\xff\xff\0\0\x3d\0\0\0\0", 91)
    g.load(memory)
    g.require(b"\x3d\0\0\0\x20\x0f\x92\xc0\x3c\x01", 92)
    g.load(memory)
    g.code.extend(b"\xc7\x80" + struct.pack("<II", 65536, 0x12345678))
    g.code.extend(b"\x05" + struct.pack("<I", 65539) + b"\xa3" + struct.pack("<I", flat))
    g.load(flat)
    g.call(1, 425)
    g.code.extend(b"\xa3" + struct.pack("<I", far))
    g.require(b"\x25\xff\xff\0\0\x3d\x03\0\0\0", 93)
    g.load(far)
    g.call(1, 426)
    g.require(b"\x3b\x05" + struct.pack("<I", flat), 94)
    g.code.extend(b"\x8b\x40\xfd")
    g.require(b"\x3d" + struct.pack("<I", 0x12345678), 95)
    g.call(1, 306, (memory,), extent, flags)
    g.result(0, 96)
    g.word(extent, 73728, 97)
    g.call(1, 304, (memory,))
    g.result(0, 98)
    g.call(1, 299, memory, 0xFFFFFFFF, 0x53)
    g.result(8, 99)
    return g.image("TILED")


def library_paths16():
    g = Guest16()
    path = g.field(b"C:\\OS2\\DLL\0")
    strict = g.field(b"T\0")
    output = g.field(bytes(1024) + struct.pack("<H", 0xA55A))
    module = g.field(bytes(2))
    procedure = g.field(bytes(4))
    for flags, value in ((1, path), (2, path), (3, strict)):
        g.pointer(value)
        g.push(0)
        g.push(flags)
        g.call(1, 998)
        g.zero(80)
        g.pointer(output)
        g.push(0)
        g.push(flags)
        g.call(1, 999)
        g.zero(81)
        g.word(output, 0x54 if flags == 3 else 0x3A43, 82)
        g.word(output + 1024, 0xA55A, 83)
    # A nonzero high flag word must reach the API instead of unbalancing SP.
    g.pointer(output)
    g.push(1)
    g.push(1)
    g.call(1, 999)
    g.require(b"\x3d" + struct.pack("<H", 87), 84)
    # Dynamic ordinal lookup follows the same owner-module aliases as static imports.
    name = g.field(b"DOSCALLS\0")
    g.pointer(name)
    g.pointer(module)
    g.call(1, 47)
    g.zero(85)
    g.code.extend(b"\xff\x36" + struct.pack("<H", module))
    g.push(0)
    g.push(749)
    g.pointer(procedure)
    g.call(1, 45)
    g.zero(86)
    g.push(0)
    g.push(437)
    g.push(0)
    g.code.extend(b"\xff\x1e" + struct.pack("<H", procedure))
    g.zero(87)
    g.exit(0)
    return g.image(False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    checked = args.backend == "unicorn"
    with tempfile.TemporaryDirectory(prefix="twoine-rexx-apis-") as temporary:
        work = Path(temporary)
        (work / "mixed.dll").write_bytes(mixed_library())
        metadata = b"\x80\x01\x05\0K\0value" + b"\0\x04\x02\0LONG\0xy"
        path = work / "attributes.dat"
        path.write_bytes(b"data")
        (work / "empty.dat").write_bytes(b"")
        attributes = struct.pack("<I", 4 + len(metadata)) + metadata
        if sys.platform == "darwin":
            subprocess.run(
                ["/usr/bin/xattr", "-wx", "com.twoine.metadata", attributes.hex(), str(path)],
                check=True,
                capture_output=True,
            )
        else:
            os.setxattr(path, "user.2ine.metadata", attributes)
        for name, image in (
            ("types", procedure_types(checked)),
            ("names-timers", names_and_timers(checked)),
            ("enumeration", enumeration(checked)),
            ("library-paths16", library_paths16()),
            ("tiled-memory", tiled_memory()),
        ):
            executable = work / (name + ".exe")
            executable.write_bytes(image)
            result = subprocess.run(
                [str(build / "lx_loader"), str(executable)],
                cwd=work,
                env={
                    **os.environ,
                    "TWOINE_BACKEND": args.backend,
                    "TWOINE_LIBPATH": str(build),
                    "LD_LIBRARY_PATH": str(build),
                },
                capture_output=True,
                timeout=15,
            )
            if result.returncode:
                raise RuntimeError(
                    f"{name} guest exit={result.returncode}\n"
                    + result.stderr.decode(errors="replace")
                )
        print(
            "REXX procedure types, filename edits, timers, packed EA enumeration and guest buffer bounds passed"
        )


if __name__ == "__main__":
    main()
