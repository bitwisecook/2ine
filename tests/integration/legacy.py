#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Run generated NE/LX guests against NLS, file APIs and loader lifetime rules."""

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


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


def path_program():
    g = FlatGuest()
    directory = g.field(b"created-by-lx\0")
    g.call(1, 270, directory, 0)  # DosCreateDir must create a queryable directory.
    g.result(0, 60)
    directory_info = g.field(bytes(24))
    g.call(1, 223, directory, 1, directory_info, 24)
    g.result(0, 61)
    g.call(1, 270, 0, 0)
    g.result(87, 62)
    info = g.field(bytes(24))
    for name, expected in (
        (b"input.txt", 0),
        (b"absent.txt", 2),
        (b"absent\\child.txt", 3),
        (b"input.txt\\child.txt", 3),
    ):
        path = g.field(name + b"\0")
        g.call(1, 223, path, 1, info, 24)  # DosQueryPathInfo
        g.result(expected, 50 + expected)
    disk, drives = g.field(bytes(4)), g.field(bytes(4))
    current, explicit = g.field(bytes(260)), g.field(bytes(260))
    size1, size2 = g.field(struct.pack("<I", 260)), g.field(struct.pack("<I", 260))
    g.call(1, 275, disk, drives)  # DosQueryCurrentDisk
    g.result(0, 54)
    g.call(1, 274, 0, current, size1)
    g.result(0, 55)
    g.call(1, 274, (disk,), explicit, size2)
    g.result(0, 56)
    g.load(size1)
    g.require(b"\x3b\x05" + struct.pack("<I", size2), 57)
    g.code.extend(b"\x89\xc1\xbe" + struct.pack("<I", current))
    g.code.extend(b"\xbf" + struct.pack("<I", explicit) + b"\xfc\xf3\xa6")
    g.require(b"", 58)  # Both forms must return the same bytes, including NUL.
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "PATHS", data=g.data)


def memory_program(checked):
    g = FlatGuest()
    size = g.field(struct.pack("<I", 0x100000))
    flags = g.field(bytes(4))
    g.call(1, 306, 0x20000, size, flags)
    g.result(0, 70)
    g.load(flags)
    g.require(b"\x25\x13\x00\x00\x00\x3d\x13\x00\x00\x00", 71)
    if checked:
        g.load(size)
        g.require(b"\x3d\x00\x00\x01\x00", 72)
        g.call(1, 306, 0xDEADBEEF, size, flags)
        g.result(487, 73)
        g.call(1, 306, 0x20000, 0xDEADBEEF, flags)
        g.result(87, 74)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "MEMQUERY", data=g.data)


def kill_thread_program(wait):
    g = FlatGuest()
    thread, ready, resumed, queue = [g.field(bytes(4)) for _ in range(4)]
    request, length, output, priority = [g.field(bytes(8)) for _ in range(4)]
    queue_name = g.field(b"\\QUEUES\\TWOINE_KILL_TEST\0")
    g.code = bytearray(b"\xe9" + bytes(4))
    worker = 0x10000 + len(g.code)
    if wait == "queue":
        g.call(3, 16, queue, 0, queue_name)
        g.result(0, 100)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", ready, 1))
    if wait == "sleep":
        g.call(1, 229, 10000)
    elif wait == "queue":
        g.call_handle(3, 9, queue, request, length, output, 0, 0, priority, 0)
    else:
        g.code.extend(b"\xeb\xfe")
    g.code.extend(b"\xc7\x05" + struct.pack("<II", resumed, 1))
    g.call(1, 234, 0, 0)
    struct.pack_into("<i", g.code, 1, len(g.code) - 5)
    g.call(1, 311, thread, worker, 0, 0, 65536)
    g.result(0, 101)
    loop = len(g.code)
    g.call(1, 229, 1)
    g.code.extend(b"\x83\x3d" + struct.pack("<I", ready) + b"\x01\x0f\x85")
    g.code.extend(struct.pack("<i", loop - len(g.code) - 4))
    g.call_handle(1, 111, thread)
    g.result(0, 102)
    g.call(1, 349, thread, 0)
    g.result(0, 103)
    g.word(resumed, 0, 104)
    g.call(1, 111, 1)
    g.result(309, 105)
    if wait == "queue":
        g.call_handle(3, 11, queue)
        g.result(0, 106)
    g.exit(0)
    return linear_image(
        g.code, g.fixups, ["DOSCALLS", "PMWIN", "QUECALLS"], "KILLTHRD", data=g.data
    )


def worker_exit_program(wait):
    g = FlatGuest()
    tib, pib, thread = [g.field(bytes(4)) for _ in range(3)]
    text, written = g.field(b"primary exit\n"), g.field(bytes(4))
    g.code = bytearray(b"\xe9" + bytes(4))
    callback = 0x10000 + len(g.code)
    g.code.extend(b"\x89\xe0")
    g.result(0x2FFE8, 97)  # Exit callbacks start on the initial process stack.
    g.call(1, 312, tib, pib)
    g.result(0, 90)
    g.load(tib)
    g.require(b"\x83\x38\xff", 98)  # No interrupted exception frames survive.
    g.code.extend(b"\x8b\x40\x0c\x8b\x00")  # TIB -> TIB2 -> thread ID.
    g.result(1, 91)
    g.call(1, 282, 1, text, 13, written)
    g.result(0, 92)
    g.call(1, 296, 3, 0)
    g.exit(96)
    worker = 0x10000 + len(g.code)
    g.call(1, 229, 50)
    g.exit(0)
    struct.pack_into("<i", g.code, 1, len(g.code) - 5)
    g.call(1, 296, 1, callback)
    g.result(0, 93)
    if wait == "messages":
        g.call(2, 763, 0)
        g.call(2, 716, 1, 0)
    g.data.extend(bytes(-len(g.data) % 4))
    registration = g.field(struct.pack("<II", 0xFFFFFFFF, callback))
    g.call(1, 354, registration)
    g.result(0, 99)
    g.call(1, 311, thread, worker, 0, 0, 65536)
    g.result(0, 94)
    g.code.extend(b"\x81\xec\x00\x10\x00\x00")  # Interrupted nested stack.
    if wait == "sleep":
        g.call(1, 229, 10000)
    elif wait == "messages":
        g.call(2, 915, 1, g.field(bytes(32)), 0, 0, 0)
    else:
        g.code.extend(b"\xeb\xfe")
    g.exit(95)  # No guest code resumes on the primary thread after termination.
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWIN"], "WORKEXIT", data=g.data)


def exit_library():
    g = FlatGuest()
    g.code.extend(b"\x83\x7c\x24\x08\x00\x75\x00")  # Register only at DLL initialization.
    branch = len(g.code) - 1
    g.code.extend(b"\xe8\x00\x00\x00\x00\x58\x05")  # Position-independent callback address.
    origin = len(g.code) - 2
    patch = len(g.code)
    g.code.extend(bytes(4) + b"\x50\x68\x01\xff\x00\x00")
    g.call(1, 296)  # DosExitList(EXLST_ADD | 0xff00, callback)
    g.code.extend(b"\x83\xc4\x08")
    g.code[branch] = len(g.code) - branch - 1
    g.code.extend(b"\xb8\x01\x00\x00\x00\xc3")
    struct.pack_into("<I", g.code, patch, len(g.code) - origin)
    g.call(1, 296, 3, 0)  # EXLST_EXIT transfers control; NULL is valid here.
    g.exit(96)  # A handler must never resume after handing off to the next one.
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "EXITDLL", library=True)


def exit_program():
    g = FlatGuest()
    module = g.field(bytes(4))
    name = g.field(b"EXITDLL\0")
    g.call(1, 318, 0, 0, name, module)
    g.result(0, 60)
    found = g.field(bytes(4))
    path = g.field(bytes(260))
    g.call(1, 320, (module,), 260, path)
    g.result(0, 64)
    for query in (g.field(b"exitdll\0"), g.field(b"EXITDLL.DLL\0"), path):
        g.call(1, 319, query, found)
        g.result(0, 62)
        g.load(found)
        g.require(b"\x3b\x05" + struct.pack("<I", module), 63)
    g.call(1, 322, (module,))  # The registered exit callback must remain executable.
    g.result(0, 61)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "EXITDLL.DLL"], "EXITLIFE", data=g.data)


def cycle_library(name, peer, missing=False):
    g = FlatGuest()
    strings = []

    def marker(text):
        g.code.extend(b"\x6a\x00\x89\xe1\x51\x68" + struct.pack("<I", len(text)))
        g.code.extend(b"\xe8\x00\x00\x00\x00\x58\x05")
        origin, patch = len(g.code) - 2, len(g.code)
        g.code.extend(bytes(4) + b"\x50\x6a\x01")
        g.call(1, 282)
        g.code.extend(b"\x83\xc4\x14")
        g.result(0, 110)
        strings.append((origin, patch, text))

    g.code.extend(b"\x83\x7c\x24\x08\x00\x0f\x85" + bytes(4))
    branch = len(g.code) - 4
    if name == "CYCLEB":
        # A's import of B must already be relocated when B's initializer calls A.
        g.call(2, 1)
        g.result(43, 111)
    marker((name[-1] + "i\n").encode())
    g.code.extend(b"\xb8\x01\x00\x00\x00\xc3")
    struct.pack_into("<i", g.code, branch, len(g.code) - branch - 4)
    marker((name[-1] + "t\n").encode())
    g.code.extend(b"\xb8\x01\x00\x00\x00\xc3")
    export = len(g.code)
    if name == "CYCLEA":
        g.call(2, 1)
        g.code.extend(b"\x40\xc3")
    else:
        g.code.extend(b"\xb8\x2a\x00\x00\x00\xc3")
    for origin, patch, text in strings:
        struct.pack_into("<I", g.code, patch, len(g.code) - origin)
        g.code.extend(text)
    return linear_image(
        g.code,
        g.fixups,
        ["DOSCALLS", peer] + (["ABSENT"] if missing else []),
        name,
        library=True,
        exports=[export],
    )


def cycle_program():
    g = FlatGuest()
    a, b, function = (g.field(bytes(4)) for _ in range(3))
    name_a, name_b = g.field(b"CYCLEA\0"), g.field(b"CYCLEB\0")
    for _ in range(2):
        g.call(1, 318, 0, 0, name_a, a)
        g.result(0, 112)
        g.call(1, 318, 0, 0, name_b, b)
        g.result(0, 113)
        g.call(1, 322, (a,))
        g.result(0, 114)
        g.call(1, 321, (a,), 1, 0, function)
        g.result(0, 115)
        g.load(function)
        g.code.extend(b"\xff\xd0")
        g.result(43, 116)
        g.call(1, 322, (b,))
        g.result(0, 117)
        g.call(1, 321, (a,), 1, 0, function)
        g.result(6, 118)
    for name in (b"BROKENA\0", b"BROKENB\0"):
        address = g.field(name)
        g.call(1, 318, 0, 0, address, a)
        g.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\x00\x00\x00", 119)
        g.call(1, 319, address, a)
        g.result(123, 120)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "CYCLETEST", data=g.data)


def tls_program():
    g = FlatGuest()
    slot, allocation = g.field(bytes(4)), g.field(bytes(4))
    g.call(1, 454, 1, slot)
    g.result(0, 80)
    g.call(1, 299, allocation, 4096, 0x13)
    g.result(0, 81)
    g.load(allocation)
    # Generate and call a tiny function in a DosAllocMem READ|WRITE page.
    g.code.extend(b"\xc7\x00\xb8\x2a\x00\x00\xc6\x40\x04\x00\xc6\x40\x05\xc3\xff\xd0")
    g.result(42, 82)
    for value, immediate, short, expected, status in (
        (0, 0, True, 0x44, 83),
        (0, -1, True, 0x11, 84),
        (0x7FFFFFFF, -1, True, 0x885, 85),
        (0x80000000, 1, False, 0x814, 86),
    ):
        g.load(slot)
        g.code.extend(b"\x89\xc1\xc7\x01" + struct.pack("<I", value))
        if short:
            g.code.extend(b"\x83\x39" + struct.pack("<b", immediate))
        else:
            g.code.extend(b"\x81\x39" + struct.pack("<i", immediate))
        g.code.extend(b"\x9c\x58\x25\xd5\x08\x00\x00")
        g.result(expected, status)
        g.code.extend(b"\x8b\x01")
        g.result(value, status + 10)
    g.call(1, 455, (slot,))
    g.result(0, 97)
    g.call(1, 304, (allocation,))
    g.result(0, 98)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "TLSCMP", data=g.data)


def chain_program(malformed=None):
    g = FlatGuest()
    for address, target, status in (
        (0x10400, 0x20080, 70),
        (0x10500, 0x200C0, 71),
        (0x10FFC, 0x20060, 72),
    ):
        g.word(address, target, status)
    g.exit(0)
    g.code.extend(bytes(4096 - len(g.code)))
    struct.pack_into("<I", g.code, 0x400, (0x500 << 20) | 0x345)
    struct.pack_into("<I", g.code, 0x500, (0xFFC << 20) | 0x385)
    struct.pack_into("<I", g.code, 0xFFC, 0xFFF00000 | 0x325)
    source, kind = 0x400, 7
    if malformed == "cycle":
        struct.pack_into("<I", g.code, 0x500, (0x400 << 20) | 0x385)
    elif malformed == "bounds":
        struct.pack_into("<I", g.code, 0x500, (0xFFD << 20) | 0x385)
    elif malformed == "overlap":
        struct.pack_into("<I", g.code, 0x500, (0x402 << 20) | 0x385)
    elif malformed == "negative":
        source = -1
    elif malformed == "type":
        kind = 8
    g.fixups.append(struct.pack("<BBhBH", kind, 8, source, 2, 0x80))
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "CHAIN", data=bytes(256))


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
        SDL_VIDEO_DRIVER="dummy",
        SDL_RENDER_DRIVER="software",
    )
    env["LD_LIBRARY_PATH"] = str(build)
    with tempfile.TemporaryDirectory(prefix="twoine-legacy-") as scratch:
        work = Path(scratch)
        (work / "cyclea.dll").write_bytes(cycle_library("CYCLEA", "CYCLEB"))
        (work / "cycleb.dll").write_bytes(cycle_library("CYCLEB", "CYCLEA"))
        (work / "brokena.dll").write_bytes(cycle_library("BROKENA", "BROKENB", missing=True))
        (work / "brokenb.dll").write_bytes(cycle_library("BROKENB", "BROKENA"))
        cycle = work / "cycle.exe"
        cycle.write_bytes(cycle_program())
        result = subprocess.run(
            [str(build / "lx_loader"), str(cycle)],
            cwd=work,
            env=env,
            capture_output=True,
            timeout=15,
        )
        if result.returncode or result.stdout != b"Bi\nAi\nAt\nBt\n" * 2:
            raise RuntimeError(
                f"circular DLL lifecycle exit={result.returncode}\n"
                + repr(result.stdout)
                + result.stderr.decode(errors="replace")
            )
        tls = work / "tlscmp.exe"
        tls.write_bytes(tls_program())
        result = subprocess.run(
            [str(build / "lx_loader"), str(tls)], cwd=work, env=env, capture_output=True, timeout=15
        )
        if result.returncode:
            raise RuntimeError(
                f"TLS comparisons exit={result.returncode}\n"
                + result.stderr.decode(errors="replace")
            )
        for malformed in (None, "cycle", "bounds", "overlap", "negative", "type"):
            chain = work / "chain.exe"
            chain.write_bytes(chain_program(malformed))
            result = subprocess.run(
                [str(build / "lx_loader"), str(chain)],
                cwd=work,
                env=env,
                capture_output=True,
                timeout=15,
            )
            if result.returncode != int(malformed is not None) or (
                malformed and b"Invalid LX internal fixup chain" not in result.stderr
            ):
                raise RuntimeError(
                    f"internal chain {malformed}: exit={result.returncode}\n"
                    + result.stderr.decode(errors="replace")
                )
        (work / "input.txt").write_bytes(b"NE16 fixture\n")
        paths = work / "paths.exe"
        paths.write_bytes(path_program())
        result = subprocess.run(
            [str(build / "lx_loader"), str(paths)],
            cwd=work,
            env=env,
            capture_output=True,
            timeout=15,
        )
        if result.returncode:
            raise RuntimeError(
                f"paths exit={result.returncode}\n" + result.stderr.decode(errors="replace")
            )
        memory = work / "memquery.exe"
        memory.write_bytes(memory_program(args.backend == "unicorn"))
        result = subprocess.run(
            [str(build / "lx_loader"), str(memory)],
            cwd=work,
            env=env,
            capture_output=True,
            timeout=15,
        )
        if result.returncode:
            raise RuntimeError(
                f"memory query exit={result.returncode}\n" + result.stderr.decode(errors="replace")
            )
        if args.backend == "unicorn":
            for wait in ("busy", "sleep", "queue"):
                killed = work / "killthrd.exe"
                killed.write_bytes(kill_thread_program(wait))
                result = subprocess.run(
                    [str(build / "lx_loader"), str(killed)],
                    cwd=work,
                    env=env,
                    capture_output=True,
                    timeout=15,
                )
                if result.returncode:
                    raise RuntimeError(
                        f"kill thread ({wait})={result.returncode}\n"
                        + result.stderr.decode(errors="replace")
                    )
            for wait in ("busy", "sleep", "messages"):
                worker_exit = work / "workexit.exe"
                worker_exit.write_bytes(worker_exit_program(wait))
                result = subprocess.run(
                    [str(build / "lx_loader"), str(worker_exit)],
                    cwd=work,
                    env=env,
                    capture_output=True,
                    timeout=15,
                )
                if result.returncode or result.stdout != b"primary exit\n":
                    raise RuntimeError(
                        f"worker exit ({wait})={result.returncode}\n"
                        + repr(result.stdout)
                        + result.stderr.decode(errors="replace")
                    )
        (work / "exitdll.dll").write_bytes(exit_library())
        lifetime = work / "exitlife.exe"
        lifetime.write_bytes(exit_program())
        result = subprocess.run(
            [str(build / "lx_loader"), str(lifetime)],
            cwd=work,
            env=env,
            capture_output=True,
            timeout=15,
        )
        if result.returncode:
            raise RuntimeError(
                f"exit callback lifetime exit={result.returncode}\n"
                + result.stderr.decode(errors="replace")
            )
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
        print("LX internal chains relocate distinct targets and reject malformed page links")


if __name__ == "__main__":
    main()
