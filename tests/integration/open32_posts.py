# Public domain. No rights reserved. Wholly AI-authored.
"""Posted Windows messages cross worker ownership and retain their original payload."""

import struct

from images import FlatGuest, linear_image
from open32_contracts import call, store


def posted_program():
    g = FlatGuest()
    name = g.field(b"PostedOwner\0")
    window, thread, tid, count, packet, first, second = (g.field(bytes(4)) for _ in range(7))
    message, native = g.field(bytes(28)), g.field(bytes(32))
    record = g.field(struct.pack("<10I", 0, 0x88776655, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, name))
    call(g, "RegisterClass", record)
    call(g, "CreateWindowEx", 0, name, 0, 0, 0, 0, 100, 50, 0, 0, 0xFFFFFFFF, 0)
    store(g, window)
    call(g, "CreateThread", 0, 0, 0x77665544, (window,), 0, tid)
    store(g, thread)
    call(g, "WaitForSingleObject", (thread,), 3000)
    g.result(0, 1)
    call(g, "CloseHandle", (thread,))
    for code, a, b in ((0x8001, 0x12345678, 0x87654321), (0x113, 55, 0)):
        call(g, "GetMessage", message, (window,), code, code)
        g.result(1, 2)
        g.word(message + 4, code, 3)
        g.word(message + 8, a, 4)
        g.word(message + 12, b, 5)
        call(g, "DispatchMessage", message)
    g.word(count, 2, 6)
    # A native PM loop can deliver the third Windows packet through WinXProcessMsg.
    g.call(3, 915, 1, native, 0, 0, 0, 1)
    g.result(1, 7)
    g.call(2, 6007, native)
    g.result(1, 8)
    g.word(count, 3, 9)
    g.word(packet, 0x8002, 10)
    g.word(first, 0x11223344, 11)
    g.word(second, 0xAABBCCDD, 12)
    call(g, "DestroyWindow", (window,))
    call(g, "CreateThread", 0, 0, 0x66554433, (window,), 0, tid)
    store(g, thread)
    call(g, "WaitForSingleObject", (thread,), 3000)
    g.result(0, 13)
    call(g, "CloseHandle", (thread,))
    call(g, "UnregisterClass", name, 0xFFFFFFFF)
    g.exit(0)
    procedure = 0x10000 + len(g.code)
    g.code.extend(b"\x81\x7c\x24\x08\x01\x80\0\0\x74\x14")
    g.code.extend(b"\x81\x7c\x24\x08\x02\x80\0\0\x74\x0a")
    g.code.extend(b"\x81\x7c\x24\x08\x13\x01\0\0\x75")
    skip = len(g.code)
    g.code.extend(b"\0")
    g.code.extend(b"\xff\x05" + struct.pack("<I", count))
    for offset, target in ((8, packet), (12, first), (16, second)):
        g.code.extend(b"\x8b\x44\x24" + bytes([offset]))
        store(g, target)
    g.code[skip] = len(g.code) - skip - 1
    g.code.extend(b"\xb8\x01\0\0\0\xc3")
    worker = 0x10000 + len(g.code)
    for code, a, b in (
        (0x8001, 0x12345678, 0x87654321),
        (0x113, 55, 0),
        (0x8002, 0x11223344, 0xAABBCCDD),
    ):
        call(g, "PostMessage", (window,), code, a, b)
        g.result(1, 14)
    g.code.extend(b"\x31\xc0\xc3")
    invalid = 0x10000 + len(g.code)
    call(g, "PostMessage", (window,), 0x8001, 0, 0)
    g.result(0, 15)
    call(g, "GetLastError")
    g.result(1400, 16)
    g.code.extend(b"\x31\xc0\xc3")
    g.data = g.data.replace(struct.pack("<I", 0x88776655), struct.pack("<I", procedure))
    for sentinel, address in ((0x77665544, worker), (0x66554433, invalid)):
        g.code = g.code.replace(struct.pack("<I", sentinel), struct.pack("<I", address))
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX", "PMWIN"], "POSTED32", data=g.data)
