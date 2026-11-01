#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise segmented suballocation/EAs and cross-process installer queue/session APIs."""

import argparse
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image
from legacy import Guest

ROOT = Path(__file__).resolve().parents[2]
FEA = struct.pack("<I", 15) + b"\0\x01\x05\0K\0value"
COMMAND_TAIL = b' /p="Lotus suite"  /x="" \\tail\\ '


def segmented():
    g = Guest()
    selector, first, second = (g.field(bytes(2)) for _ in range(3))
    handle, action = g.field(bytes(2)), g.field(bytes(2))
    filename = g.field(b"attributes.dat\0")
    attributes = g.field(FEA)
    operation = g.field(struct.pack("<III", 0, attributes, 0))
    status = g.field(bytes(26))
    returned = g.field(struct.pack("<I", 4) + bytes(28))

    def value(address):
        g.code.extend(b"\xff\x36" + struct.pack("<H", address))

    def set_heap(flags, size, result=0):
        value(selector)
        g.push(flags)
        g.push(size)
        g.call(1, 146)
        g.require(b"\x3d" + struct.pack("<H", result), 10 + flags)

    def allocate(output, size, result=0):
        value(selector)
        g.pointer(output)
        g.push(size)
        g.call(1, 147)
        g.require(b"\x3d" + struct.pack("<H", result), 20)

    def release(offset, size, result=0):
        value(selector)
        value(offset)
        g.push(size)
        g.call(1, 148)
        g.require(b"\x3d" + struct.pack("<H", result), 21)

    g.push(256)
    g.pointer(selector)
    g.push(0)
    g.call(1, 34)
    g.zero(1)
    set_heap(1, 128)
    allocate(first, 1)
    g.word(first, 8, 2)
    allocate(second, 9)
    g.word(second, 12, 3)
    allocate(first, 0, 313)
    release(first, 1)
    release(first, 1, 312)
    allocate(first, 4)
    g.word(first, 8, 4)
    set_heap(2, 128, 314)
    set_heap(0, 64, 310)
    set_heap(0, 256)
    # A real packed EAOP holds a 16:16 FEALIST pointer.
    g.code.extend(b"\x8c\xd8\xa3" + struct.pack("<H", operation + 6))
    g.pointer(filename)
    g.pointer(handle)
    g.pointer(action)
    g.push(0)
    g.push(0)
    g.push(0)
    g.push(0x12)
    g.push(0)
    g.push(0x42)
    g.pointer(operation)
    g.push(0)
    g.push(0)
    g.call(1, 95)
    g.zero(30)
    g.word(action, 2, 31)
    value(handle)
    g.push(2)
    g.pointer(status)
    g.push(26)
    g.call(1, 74)
    g.zero(33)
    g.word(status + 22, len(FEA), 34)
    g.code.extend(b"\xc7\x06" + struct.pack("<HH", operation + 4, returned))

    def query_all(result, code):
        value(handle)
        g.push(4)
        g.pointer(operation)
        g.push(12)
        g.call(1, 74)
        g.require(b"\x3d" + struct.pack("<H", result), code)

    query_all(111, 35)
    g.word(returned, len(FEA), 36)
    g.code.extend(b"\xc7\x06" + struct.pack("<HH", returned, 32))
    query_all(0, 37)
    for offset in range(0, len(FEA) - 1, 2):
        g.word(returned + offset, struct.unpack_from("<H", FEA, offset)[0], 38)
    value(handle)
    g.call(1, 59)
    g.zero(32)
    empty_name = g.field(b"empty-attributes.dat\0")
    g.pointer(empty_name)
    g.pointer(handle)
    g.pointer(action)
    for number in (0, 0, 0, 0x12, 0, 0x42, 0, 0, 0, 0):
        g.push(number)
    g.call(1, 95)
    g.zero(39)
    g.code.extend(b"\xc7\x06" + struct.pack("<HH", returned, 32))
    query_all(0, 40)
    g.word(returned, 4, 41)
    value(handle)
    g.call(1, 59)
    g.zero(42)
    search, count = g.field(bytes(2)), g.field(bytes(2))
    found = g.field(bytes(300))
    large = g.field(b"large.dat\0")
    for level, name, name_offset, ea_offset in (
        (2, filename, 27, 22),
        (11, large, 37, None),
        (12, filename, 41, 36),
        (12, large, 41, 36),
    ):
        for field, number in ((search, 0xFFFF), (count, 1)):
            g.code.extend(b"\xc7\x06" + struct.pack("<HH", field, number))
        g.pointer(name)
        g.pointer(search)
        g.push(0)
        g.pointer(found)
        g.push(300)
        g.pointer(count)
        g.push(level)
        g.push(0)
        g.push(0)
        g.call(1, 184)
        g.zero(43)
        g.word(count, 1, 44)
        g.word(found + name_offset, 0x616C if name == large else 0x7461, 45)
        if ea_offset is not None:
            g.word(found + ea_offset, 4 if name == large else len(FEA), 46)
        if name == large:
            g.word(found + 16, 7, 47)
            g.word(found + 20, 1, 48)
        value(search)
        g.call(1, 63)
        g.zero(49)
    edited = g.field(bytes(64))
    for source, mask, expected in (
        (b"file.txt", b"*E.TMP", b"filE.TMP"),
        (b"foo.bar", b"*.baz", b"foo.baz"),
        (b"hello", b"*.*", b"hello"),
        (b"abc.txt", b"??.bin", b"ab.bin"),
    ):
        g.push(1)
        g.pointer(g.field(source + b"\0"))
        g.pointer(g.field(mask + b"\0"))
        g.pointer(edited)
        g.push(64)
        g.call(1, 191)
        g.zero(50)
        padded = expected + b"\0\0"
        for offset in range(0, len(expected) + 1, 2):
            g.word(edited + offset, struct.unpack_from("<H", padded, offset)[0], 51)
    g.exit(0)
    return g.image(True)


def queue_program(writer):
    g = FlatGuest()
    event_name = g.field(b"\\SEM32\\Bottle_Install_Test\0")
    folded_name = g.field(b"\\sem32\\bottle_install_test\0")
    event, duplicate, posts = (g.field(bytes(4)) for _ in range(3))
    if writer:
        g.call(1, 325, folded_name, event)
    else:
        g.call(1, 324, event_name, event, 0, 0)
    g.result(0, 100)
    if not writer:
        g.call(1, 324, folded_name, duplicate, 0, 0)
        g.result(285, 101)  # ERROR_DUPLICATE_NAME
        g.call_handle(1, 329, event, 0)
        g.result(640, 102)
    else:
        g.call_handle(1, 328, event)
        g.result(0, 103)
    name = g.field(b"\\QUEUES\\BOTTLE_INSTALL_TEST\0")
    queue, owner, count = (g.field(bytes(4)) for _ in range(3))
    gate_name = g.field(b"\\SEM32\\BOTTLE_SESSION_GATE\0")
    gate = g.field(bytes(4))
    shared_name = g.field(b"\\SHAREMEM\\BOTTLE_INSTALL_TEST\0")
    shared, again = g.field(bytes(4)), g.field(bytes(4))
    hab, pmqueue, pmwindow = (g.field(bytes(4)) for _ in range(3))
    g.call(4, 763, 0)
    g.code.extend(b"\xa3" + struct.pack("<I", hab))
    if not writer:
        g.call_handle(4, 716, hab, 0)
        g.code.extend(b"\xa3" + struct.pack("<I", pmqueue))
        g.call(4, 909, 2, 0xFFFF0005, 0, 1, 0, 0, 10, 10, 0, 3, 123, 0, 0)
        g.code.extend(b"\xa3" + struct.pack("<I", pmwindow))
        g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 122)
    g.call(1, 301 if writer else 300, shared, shared_name, *([3] if writer else [4096, 19]))
    g.result(0, 70)
    g.load(shared)
    if writer:
        g.require(b"\x3b\x00", 71)
        g.code.extend(b"\x8b\x50\x0c")
        g.require(b"\x3b\x15" + struct.pack("<I", event), 123)
        g.call(1, 325, folded_name, duplicate)
        g.result(0, 124)
        g.load(duplicate)
        g.require(b"\x3b\x05" + struct.pack("<I", event), 125)
        g.call_handle(1, 326, duplicate)
        g.result(0, 126)
        g.call_handle(1, 328, event)
        g.result(299, 127)
        g.load(shared)
        g.require(b"\x81\x78\x04" + struct.pack("<I", 0x1234ABCD), 72)
        g.code.extend(b"\xc7\x40\x04" + struct.pack("<I", 0xABCDEF12))
        g.code.extend(b"\x8b\x40\x08")
        g.code.extend(b"\xa3" + struct.pack("<I", pmwindow))
        g.call(4, 772, (hab,), (pmwindow,))
        g.result(1, 130)
        display = g.field(bytes(4))
        g.call(5, 263, (pmwindow,))
        g.code.extend(b"\xa3" + struct.pack("<I", display))
        g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 131)
        g.call(5, 269, (display,), 90)
        g.result(96, 132)
        g.call(5, 560, (pmwindow,), (display,))
        g.result(1, 133)
        g.call_handle(4, 919, pmwindow, 0x5151, 0x1234ABCD, (shared,))
        g.result(1, 115)
        g.call_handle(4, 919, pmwindow, 0x5152, 0xABCDEF12, 0)
        g.result(1, 150)
    else:
        g.code.extend(b"\x89\x00\xc7\x40\x04" + struct.pack("<I", 0x1234ABCD))
        g.code.extend(b"\x8b\x15" + struct.pack("<I", pmwindow) + b"\x89\x50\x08")
        g.code.extend(b"\x8b\x15" + struct.pack("<I", event) + b"\x89\x50\x0c")
    g.call(1, 301, again, shared_name, 3)
    g.result(0, 73)
    region_size = g.field(struct.pack("<I", 65536))
    region_flags = g.field(bytes(4))
    g.call(1, 306, (shared,), region_size, region_flags)
    g.result(0, 76)
    g.word(region_size, 4096, 77)
    g.word(region_flags, 0x12013, 78)
    g.load(shared)
    g.require(b"\x3b\x05" + struct.pack("<I", again), 74)
    if writer:
        g.call(2, 15, owner, queue, name)
        g.result(0, 10)
        g.call(1, 325, gate_name, gate)
        g.result(0, 143)
        g.call_handle(1, 329, gate, 1000)
        g.result(0, 144)
        g.call_handle(1, 326, gate)
        g.result(0, 145)
        grandchild = g.field(b"C:\\OS2\\grand.exe\0")
        command = g.field(b"C:\\OS2\\grand.exe\0" + COMMAND_TAIL + b"\0")
        result = g.field(bytes(8))
        g.call(1, 283, 0, 0, 0, command, 0, result, grandchild)
        g.result(0, 11)
        g.word(result + 4, 0, 13)
        g.call_handle(2, 11, queue)
        g.result(0, 12)
    else:
        g.call(2, 16, queue, 4, name)
        g.result(0, 20)
        g.call(2, 16, owner, 4, name)
        g.result(332, 21)
        request, size, pointer, priority = (g.field(bytes(n)) for n in (8, 4, 4, 1))
        notification = g.field(bytes(4))
        g.call(1, 324, 0, notification, 0, 0)
        g.result(0, 134)
        g.call_handle(2, 9, queue, request, size, pointer, 0, 1, priority, (notification,))
        g.result(342, 22)
        g.call_handle(2, 9, queue, request, size, pointer, 0, 1, priority, (event,))
        g.result(87, 135)
        directory = g.field(b"C:\\TEMP\0")
        g.call(1, 255, directory)
        g.result(0, 155)
        filename = g.field(b"writer.exe\0")
        session = g.field(
            struct.pack(
                "<4H5I2HII6HII",
                60,
                1,
                1,
                0,
                0,
                filename,
                0,
                name,
                0,
                1,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
            )
        )
        sid, pid = g.field(bytes(4)), g.field(bytes(4))
        g.call(3, 38, 0)
        g.result(224, 128)  # No active top-level PM window in this object-only parent.
        g.call(3, 38, 0xFFFFFFFF)
        g.result(369, 129)
        g.call(1, 324, gate_name, gate, 0, 0)
        g.result(0, 146)
        g.call(3, 37, session, sid, pid)
        g.result(0, 23)
        g.load(sid)
        g.require(b"\xa9\x00\x00\xff\xff", 153)
        for length, select, bond, expected in (
            (5, 1, 1, 461),
            (6, 3, 1, 456),
            (6, 1, 3, 455),
            (6, 2, 1, 0),
            (6, 1, 2, 0),
        ):
            status = g.field(struct.pack("<3H", length, select, bond))
            g.call(3, 39, (sid,), status)
            g.result(expected, 147)
        g.call_handle(1, 328, gate)
        g.result(0, 148)
        g.call_handle(2, 9, queue, request, size, pointer, 0, 0, priority, 0)
        g.result(0, 24)
        g.word(request + 4, 0xCAFE, 25)
        g.word(size, 0, 26)
        g.load(shared)
        g.require(b"\x81\x78\x04" + struct.pack("<I", 0xABCDEF12), 75)
        g.call_handle(2, 9, queue, request, size, pointer, 0, 0, priority, 0)
        g.result(0, 27)
        g.word(size, 4, 28)
        g.load(pointer)
        g.require(b"\x66\x83\x78\x02\x00", 29)
        g.code.extend(b"\x0f\xb7\x00")
        g.require(b"\x3b\x05" + struct.pack("<I", sid), 154)
        g.call(1, 304, (pointer,))
        g.result(0, 30)
        g.call_handle(2, 12, queue, count)
        g.result(0, 31)
        g.word(count, 0, 32)
        g.call_handle(1, 329, notification, 1000)
        g.result(0, 136)
        g.call_handle(1, 327, notification, count)
        g.result(0, 137)
        g.call_handle(2, 14, queue, 0xBEEF, 0, 0, 0)
        g.result(0, 138)
        g.call_handle(1, 329, notification, 1000)
        g.result(0, 139)
        g.call_handle(2, 9, queue, request, size, pointer, 0, 1, priority, (notification,))
        g.result(0, 140)
        g.word(request + 4, 0xBEEF, 141)
        g.call_handle(2, 11, queue)
        g.result(0, 33)
        g.call_handle(1, 326, notification)
        g.result(0, 142)
        g.call_handle(1, 326, gate)
        g.result(0, 149)
        message = g.field(bytes(32))
        g.call_handle(4, 915, hab, message, (pmwindow,), 0x5151, 0x5151)
        g.result(1, 116)
        g.word(message + 4, 0x5151, 117)
        g.word(message + 8, 0x1234ABCD, 118)
        g.load(shared)
        g.require(b"\x3b\x05" + struct.pack("<I", message + 12), 119)
        g.call_handle(4, 915, hab, message, (pmwindow,), 0x5152, 0x5152)
        g.result(1, 151)
        g.word(message + 8, 0xABCDEF12, 152)
        g.call_handle(4, 728, pmwindow)
        g.result(1, 120)
        g.call_handle(4, 919, pmwindow, 0x5151, 0, 0)
        g.result(0, 121)
    if not writer:
        g.call_handle(1, 329, event, 1000)
        g.result(0, 104)
        g.call_handle(1, 330, event, posts)
        g.result(0, 105)
        g.word(posts, 2, 106)
        g.call_handle(1, 327, event, posts)
        g.result(0, 107)
        g.word(posts, 2, 108)
        g.call_handle(1, 329, event, 0)
        g.result(640, 109)
    g.call_handle(1, 326, event)
    g.result(0, 110)
    if not writer:
        g.call(1, 325, event_name, event)
        g.result(187, 111)  # ERROR_SEM_NOT_FOUND
        g.call(1, 324, event_name, event, 0, 1)
        g.result(0, 112)
        g.call_handle(1, 329, event, 0)
        g.result(0, 113)
        g.call_handle(1, 326, event)
        g.result(0, 114)
    g.code.extend(
        b"\xb8" + struct.pack("<I", shared_name) + b"\xb9" + struct.pack("<I", 0x12345678)
    )
    for ordinal in (425, 426):
        g.code.extend(b"\xe8\0\0\0\0")
        g.fixups.append(struct.pack("<BBhBH", 8, 1, len(g.code) - 4, 1, ordinal))
    g.result(shared_name, 80)
    g.require(b"\x81\xf9" + struct.pack("<I", 0x12345678), 81)
    g.exit(0)
    return linear_image(
        g.code,
        g.fixups,
        ["DOSCALLS", "QUECALLS", "SESMGR", "PMWIN", "PMWINX"],
        "WRITER" if writer else "QUEUES",
        data=g.data,
    )


def grandchild_program():
    g = FlatGuest()
    tib, pib = g.field(bytes(4)), g.field(bytes(4))
    g.call(1, 312, tib, pib)
    g.result(0, 130)
    g.load(pib)
    g.code.extend(b"\x8b\x40\x0c\x83\xc0" + bytes([len(b"C:\\OS2\\grand.exe\0")]))
    for i in range(0, len(COMMAND_TAIL) - 3, 4):
        g.require(b"\x81\x78" + bytes([i]) + COMMAND_TAIL[i : i + 4], 131 + i)
    name = g.field(b"\\QUEUES\\BOTTLE_INSTALL_TEST\0")
    owner, queue, pointer = (g.field(bytes(4)) for _ in range(3))
    path = g.field(b"PATH\0")
    g.call(1, 227, path, pointer)
    g.result(0, 10)
    g.load(pointer)
    g.require(b"\x81\x38" + struct.pack("<I", int.from_bytes(b"C:\\O", "little")), 11)
    g.call(2, 15, owner, queue, name)
    g.result(0, 12)
    g.call_handle(2, 14, queue, 0xCAFE, 0, 0, 5)
    g.result(0, 13)
    g.call_handle(2, 11, queue)
    g.result(0, 14)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "QUECALLS"], "GRAND", data=g.data)


def fixed_selectors():
    g = FlatGuest()
    marker = g.field(bytes(4))
    g.code.extend(b"\xea" + struct.pack("<IH", 0x10000 + len(g.code) + 7, 0x5B))
    # Use OS/2's flat data selector, write through it and execute an imported API
    # with the fixed flat code selector still active.
    g.code.extend(b"\x66\xb8\x53\x00\x8e\xd8\xc7\x05" + struct.pack("<II", marker, 0xC0FFEE))
    g.word(marker, 0xC0FFEE, 90)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS"], "FLATSEL", data=g.data)


def termination_program(sleeper):
    g = FlatGuest()
    if sleeper:
        g.call(1, 229, 60000)
        g.exit(99)
    else:
        executable = g.field(b"C:\\OS2\\sleeper.exe\0")
        arguments = g.field(b"C:\\OS2\\sleeper.exe\0\0")
        result, status, pid = g.field(bytes(8)), g.field(bytes(8)), g.field(bytes(4))
        g.call(1, 235, 0, 0)
        g.result(303, 150)
        g.call(1, 283, 0, 0, 2, arguments, 0, result, executable)
        g.result(0, 151)
        g.call(1, 235, 0, (result,))
        g.result(0, 152)
        g.call(1, 280, 0, 0, status, pid, (result,))
        g.result(0, 153)
        g.word(status, 3, 154)  # TC_KILLPROCESS
        g.call(1, 235, 0, (result,))
        g.result(303, 155)
        g.exit(0)
    return linear_image(
        g.code, g.fixups, ["DOSCALLS"], "SLEEPER" if sleeper else "KILL", data=g.data
    )


def segmented_lx():
    g = Guest()
    marker = g.field(struct.pack("<H", 0xBEEF))
    saved_stack = g.field(bytes(2))
    tib, pib = g.field(bytes(4)), g.field(bytes(4))
    g.word(marker, 0xBEEF, 90)
    g.push(40960)
    g.code.extend(b"\x1e")
    g.call(1, 38)  # Dos16ReallocSeg grows a smaller loaded LX data tile in place.
    g.zero(98)
    g.code.extend(b"\xc7\x06\xfe\x9f\x34\x12")
    g.word(0x9FFE, 0x1234, 99)
    # A USE16 LX object uses its declared automatic data and stack objects.
    g.code.extend(b"\x89\x26" + struct.pack("<H", saved_stack))
    g.pointer(tib)
    g.pointer(pib)
    g.code.extend(b"\x66\xb8\0\0\0\0")
    target_offset = len(g.code) - 4
    # Two far pointer arguments, DWORD return; the wrapper jumps into UNITHUNK.
    g.code.extend(b"\x66\xba" + struct.pack("<I", 0x152))
    g.call(1, 576)
    g.zero(91)
    g.code.extend(b"\x89\xe0")
    g.require(b"\x3b\x06" + struct.pack("<H", saved_stack), 94)
    g.word(marker, 0xBEEF, 92)
    for output in (tib, pib):
        g.code.extend(b"\x66\xa1" + struct.pack("<H", output))
        g.require(b"\x66\x85\xc0\x0f\x95\xc0\x3c\x01", 93)
    for value in (0x1234, 0x5678, 0xFFFE, 0xFF80):
        g.push(value)
    g.pointer(marker)
    g.code.extend(b"\x66\xb8\0\0\0\0")
    arithmetic_target = len(g.code) - 4
    g.code.extend(b"\x66\xba" + struct.pack("<I", 0x394))
    g.call(1, 576)
    g.require(b"\x66\x3d" + struct.pack("<I", 0x123755F6), 95)
    g.require(b"\x81\xfa\x37\x12", 96)
    g.code.extend(b"\x89\xe0")
    g.require(b"\x3b\x06" + struct.pack("<H", saved_stack), 97)
    g.exit(0)
    struct.pack_into("<I", g.code, arithmetic_target, 0x10000 + len(g.code))
    g.code.extend(b"\x8b\x44\x24\x04\x03\x44\x24\x08\x03\x44\x24\x0c\x03\x44\x24\x10\xc3")
    fixups = []
    for record in g.fixups:
        _, _, offset, module, ordinal = struct.unpack("<BBHHH", record)
        fixups.append(struct.pack("<BBhBH", 0x13, 1, offset, module, ordinal))
    fixups.append(struct.pack("<BBhBH", 7, 1, target_offset, 1, 312))
    image = linear_image(g.code, fixups, ["DOSCALLS"], "SEGMENTEDLX", data=g.data)
    # USE16 object flags and their required 16:16 aliases.
    struct.pack_into("<I", image, 64 + 196 + 8, 0x1005)
    struct.pack_into("<I", image, 64 + 196 + 24 + 8, 0x1003)
    struct.pack_into("<I", image, 64 + 196 + 24, 32768)
    struct.pack_into("<I", image, 64 + 36, 32752)
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="twoine-install-apis-") as temporary:
        bottle = Path(temporary) / "bottle"
        base = [
            sys.executable,
            str(ROOT / "tools/bottles.py"),
            "--bottle",
            str(bottle),
            "--build",
            str(args.build.resolve()),
            "--backend",
            args.backend,
        ]
        subprocess.run([*base, "init"], check=True, capture_output=True)
        directory = bottle / "drive_c/OS2"
        with (directory / "large.dat").open("wb") as sparse:
            sparse.truncate((1 << 32) + 7)
        for filename, data in (
            ("segmented.exe", segmented()),
            ("segmented-lx.exe", segmented_lx()),
            ("writer.exe", queue_program(True)),
            ("queues.exe", queue_program(False)),
            ("sleeper.exe", termination_program(True)),
            ("kill.exe", termination_program(False)),
        ):
            (directory / filename).write_bytes(data)
        (directory / "grand.exe").write_bytes(grandchild_program())
        executables = ["segmented.exe", "segmented-lx.exe", "queues.exe", "kill.exe"]
        if args.backend == "unicorn":
            (directory / "selectors.exe").write_bytes(fixed_selectors())
            executables.append("selectors.exe")
        for executable in executables:
            result = subprocess.run(
                [*base, "run", "C:\\OS2\\" + executable],
                capture_output=True,
                timeout=15,
                env=dict(os.environ, SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy"),
            )
            if result.returncode:
                raise AssertionError(
                    f"{executable}: {result.returncode}\n{result.stderr.decode(errors='replace')}"
                )
        attribute = "com.twoine.metadata" if sys.platform == "darwin" else "user.2ine.metadata"
        assert os.getxattr(directory / "attributes.dat", attribute) == FEA
        print(
            "Segmented suballocation, persistent packed EAs and cross-process queue/session handoff passed"
        )


if __name__ == "__main__":
    main()
