#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Run generated NE/LX guests against NLS, file APIs and loader lifetime rules."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path


class Guest:
    def __init__(self):
        self.code = bytearray()
        self.data = bytearray()
        self.fixups = []

    def field(self, value):
        offset = len(self.data)
        self.data.extend(value)
        return offset

    def push(self, value):
        self.code.extend(b"\x68" + struct.pack("<H", value))

    def pointer(self, offset):
        self.code.extend(b"\x1e")  # push ds
        self.push(offset)

    def call(self, module, ordinal):
        self.code.extend(b"\x9a\xff\xff\x00\x00")
        self.fixups.append(struct.pack("<BBHHH", 3, 1, len(self.code) - 4, module, ordinal))

    def exit(self, status):
        self.push(1)
        self.push(status)
        self.call(1, 5)  # Dos16Exit

    def require(self, comparison, status):
        self.code.extend(comparison + b"\x74\x0b")  # je past the failure exit
        self.exit(status)

    def zero(self, status):
        self.require(b"\x09\xc0", status)  # or ax,ax

    def word(self, address, value, status):
        self.require(b"\x81\x3e" + struct.pack("<HH", address, value), status)

    def image(self, preload):
        # Two fixed segments: USE16 code and writable automatic data/stack.
        header = bytearray(64)
        header[:2] = b"NE"
        struct.pack_into("<HH", header, 4, 80, 2)  # empty entry table
        struct.pack_into("<HHHHHHHH", header, 12, 0, 2, 0, 8192, 0, 1, 0xF000, 2)
        struct.pack_into("<HHHHHHHHH", header, 28, 2, 2, 0, 64, 82, 82, 92, 96, 0)
        struct.pack_into("<H", header, 50, 4)  # 16-byte file sectors
        header[54] = 1  # OS/2
        tables = b"\x00\x00" + b"\x06LEGACY\x00\x00\x00" + struct.pack("<HH", 0, 9)
        tables += b"\x08DOSCALLS\x03NLS"
        # A DOS stub header followed by the NE header and tables.
        mz = bytearray(64)
        mz[:2] = b"MZ"
        struct.pack_into("<I", mz, 60, 64)
        prefix_size = 64 + 64 + 16 + len(tables)
        code_offset = (prefix_size + 15) & ~15
        relocations = struct.pack("<H", len(self.fixups)) + b"".join(self.fixups)
        data_offset = (code_offset + len(self.code) + len(relocations) + 15) & ~15
        segments = struct.pack("<HHHH", code_offset >> 4, len(self.code), 0x0D00, len(self.code))
        segments += struct.pack(
            "<HHHH", data_offset >> 4, len(self.data), 0x0C01 | (0x40 if preload else 0), 0xFFFF
        )
        image = mz + header + segments + tables
        image.extend(bytes(code_offset - len(image)))
        image.extend(self.code + relocations)
        image.extend(bytes(data_offset - len(image)))
        return image + self.data


def program():
    g = Guest()
    country = g.field(bytes(4))
    text = g.field(b"aZ09!\x00")
    info = g.field(bytes(38))
    actual = g.field(bytes(2))
    name = g.field(b"input.txt\x00")
    directory = g.field(b"\xff\xff")
    count = g.field(b"\x01\x00")
    found = g.field(bytes(300))
    handle = g.field(bytes(2))
    action = g.field(bytes(2))
    ea = g.field(bytes(12))
    content = g.field(bytes(4))

    g.push(5)
    g.pointer(country)
    g.pointer(text)
    g.call(2, 1)  # NLS DosCaseMap: uppercase, not lowercase
    g.zero(10)
    g.word(text, 0x5A41, 11)
    g.push(38)
    g.pointer(country)
    g.pointer(info)
    g.pointer(actual)
    g.call(2, 3)  # NLS DosGetCtryInfo
    g.zero(12)
    g.word(info, 1, 13)
    g.word(info + 2, 437, 14)
    g.word(actual, 38, 15)

    g.pointer(name)
    g.pointer(directory)
    g.push(0)
    g.pointer(found)
    g.push(300)
    g.pointer(count)
    g.push(1)  # FIL_STANDARD uses the original packed FILEFINDBUF layout.
    g.push(0)
    g.push(0)
    g.call(1, 184)  # Dos16FindFirst2
    g.zero(20)
    g.word(count, 1, 21)
    g.word(found + 23, ord("i") | ord("n") << 8, 22)
    g.code.extend(b"\xff\x36" + struct.pack("<H", directory))
    g.call(1, 63)  # Dos16FindClose
    g.zero(23)

    g.pointer(name)
    g.pointer(handle)
    g.pointer(action)
    g.push(0)
    g.push(0)  # ULONG initial size
    g.push(0)  # attributes
    g.push(1)  # open existing
    g.push(0)
    g.push(0x40)  # ULONG open mode: read, deny none
    g.pointer(ea)  # an empty, non-NULL 12-byte EAOP
    g.push(0)
    g.push(0)
    g.call(1, 95)  # Dos16Open2
    g.zero(30)
    g.word(action, 1, 31)
    g.code.extend(b"\xff\x36" + struct.pack("<H", handle))
    g.pointer(content)
    g.push(4)
    g.pointer(actual)
    g.call(1, 137)  # Dos16Read
    g.zero(32)
    g.word(actual, 4, 33)
    g.word(content, 0x454E, 34)  # "NE"
    g.code.extend(b"\xff\x36" + struct.pack("<H", handle))
    g.call(1, 59)  # Dos16Close
    g.zero(35)
    g.exit(0)
    return g


def linear_image(code, fixups, modules, name, library=False):
    header = bytearray(196)
    header[:2] = b"LX"
    struct.pack_into("<HH", header, 8, 2, 1)
    for offset, value in {
        16: 0x8000 if library else 0,
        20: 1,
        24: 1,
        32: 2,
        36: 0xFFF0,
        40: 4096,
        64: 196,
        68: 2,
        72: 244,
        88: 252,
        148: 2,
    }.items():
        struct.pack_into("<I", header, offset, value)
    tables = bytearray(struct.pack("<IIIIII", 4096, 0x10000, 0x2005, 1, 1, 0))
    tables.extend(struct.pack("<IIIIII", 65536, 0x20000, 0x2003, 2, 0, 0))
    tables.extend(struct.pack("<IHH", 0, len(code), 0))
    tables.extend(bytes([len(name)]) + name.encode() + b"\x00\x00\x00")
    struct.pack_into("<I", header, 92, len(header) + len(tables))
    tables.extend(b"\x00\x00")
    struct.pack_into("<I", header, 104, len(header) + len(tables))
    records = b"".join(fixups)
    tables.extend(struct.pack("<II", 0, len(records)))
    struct.pack_into("<I", header, 108, len(header) + len(tables))
    tables.extend(records)
    struct.pack_into("<II", header, 112, len(header) + len(tables), len(modules))
    for module in modules:
        tables.extend(bytes([len(module)]) + module.encode())
    struct.pack_into("<I", header, 120, len(header) + len(tables))
    tables.extend(b"\x00")
    mz = bytearray(64)
    mz[:2] = b"MZ"
    struct.pack_into("<I", mz, 60, 64)
    data_offset = (64 + len(header) + len(tables) + 511) & ~511
    struct.pack_into("<I", header, 128, data_offset)
    image = mz + header + tables
    image.extend(bytes(data_offset - len(image)))
    return image + code


def critical_program(checked):
    code, fixups = bytearray(), []

    def call(ordinal, pointer=None):
        if pointer is not None:
            code.extend(b"\x68" + struct.pack("<I", pointer))
        code.extend(b"\xe8\x00\x00\x00\x00")
        fixups.append(struct.pack("<BBhBH", 8, 1, len(code) - 4, 1, ordinal))
        if pointer is not None:
            code.extend(b"\x83\xc4\x04")

    def exit(status):
        code.extend(b"\x68" + struct.pack("<I", status) + b"\x68\x01\x00\x00\x00")
        call(234)

    def require(comparison, status):
        code.extend(comparison + b"\x74\x0f")
        exit(status)

    def zero(status):
        require(b"\x85\xc0", status)

    for ordinal in (232, 232, 233, 233):
        call(ordinal)
        zero(40)
    for ordinal, count in ((380, 1), (380, 2), (381, 1), (381, 0)):
        call(ordinal, 0x20000)
        zero(41)
        require(b"\x83\x3d\x00\x00\x02\x00" + bytes([count]), 42)
    if checked:
        call(380, 0xDEADBEEF)
        require(b"\x83\xf8\x57", 43)  # checked portable output pointer
    call(381, 0x20000)
    require(b"\x3d" + struct.pack("<I", 300), 44)  # ERROR_ALREADY_RESET
    exit(0)
    return linear_image(code, fixups, ["DOSCALLS"], "CRITICAL")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    env = dict(os.environ, TWOINE_BACKEND=args.backend, TWOINE_LIBPATH=str(build))
    env["LD_LIBRARY_PATH"] = str(build)
    with tempfile.TemporaryDirectory(prefix="twoine-legacy-") as scratch:
        work = Path(scratch)
        (work / "input.txt").write_bytes(b"NE16 fixture\n")
        guest = program()
        for preload in (False, True):
            exe = work / "legacy.exe"
            exe.write_bytes(guest.image(preload))
            result = subprocess.run(
                [str(build / "lx_loader"), str(exe)],
                cwd=work,
                env=env,
                capture_output=True,
                timeout=15,
            )
            if result.returncode:
                raise RuntimeError(
                    f"preload={preload}, exit={result.returncode}\n"
                    + result.stdout.decode(errors="replace")
                    + result.stderr.decode(errors="replace")
                )
        critical = work / "critical.exe"
        critical.write_bytes(critical_program(args.backend == "unicorn"))
        result = subprocess.run(
            [str(build / "lx_loader"), str(critical)],
            cwd=work,
            env=env,
            capture_output=True,
            timeout=15,
        )
        if result.returncode:
            raise RuntimeError(
                f"critical exit={result.returncode}\n" + result.stderr.decode(errors="replace")
            )
        # The first DLL initializes, then a missing sibling forces process-load rollback.
        (work / "detach.dll").write_bytes(
            linear_image(b"\xb8\x01\x00\x00\x00\xc3", [], [], "DETACH", library=True)
        )
        missing = work / "missing.exe"
        missing.write_bytes(linear_image(b"\xc3", [], ["DETACH", "ABSENT"], "MISSING"))
        result = subprocess.run(
            [str(build / "lx_loader"), str(missing)],
            cwd=work,
            env=env,
            capture_output=True,
            timeout=15,
        )
        if result.returncode != 1 or b"Failed to load dependency 'ABSENT'" not in result.stderr:
            raise RuntimeError(
                f"rollback exit={result.returncode}\n" + result.stderr.decode(errors="replace")
            )
        print("NLS ordinals/case mapping, NE preload stack, FindFirst2 and Open2 passed")
        print("LX critical-section nesting, checked must-complete counts and DLL rollback passed")


if __name__ == "__main__":
    main()
