# Public domain. No rights reserved. Wholly AI-authored.
"""Standard scrollbar pixels, line commands and thumb tracking notifications."""

import struct

from images import FlatGuest, linear_image
from open32_contracts import call, store


def scroll_program():
    g = FlatGuest()
    name, scroll = g.field(b"ScrollOwner\0"), g.field(b"SCROLLBAR\0")
    parent, child, count, packet, sender, dc = (g.field(bytes(4)) for _ in range(6))
    record = g.field(struct.pack("<10I", 0, 0x88776655, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, name))
    call(g, "RegisterClass", record)
    call(g, "CreateWindowEx", 0, name, 0, 0x10000000, 0, 0, 100, 120, 0, 0, 0xFFFFFFFF, 0)
    store(g, parent)
    call(g, "CreateWindowEx", 0, scroll, 0, 0x50000001, 0, 0, 16, 100, (parent,), 1, 0xFFFFFFFF, 0)
    store(g, child)
    g.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 1)
    call(g, "SetScrollRange", (child,), 2, 0, 100, 0)
    g.result(1, 2)
    call(g, "SetScrollPos", (child,), 2, 50, 0)
    g.result(0, 3)
    call(g, "RedrawWindow", (child,), 0, 0, 0x101)
    call(g, "GetDC", (child,))
    store(g, dc)
    call(g, "GetPixel", (dc,), 8, 6)
    g.result(0, 4)
    call(g, "GetPixel", (dc,), 8, 50)
    g.result(0xCCCCCC, 5)
    call(g, "ReleaseDC", (child,), (dc,))
    call(g, "SendMessage", (child,), 0x201, 1, 8 | 4 << 16)
    g.word(count, 1, 6)
    g.word(packet, 0, 7)
    g.load(sender)
    g.require(b"\x3b\x05" + struct.pack("<I", child), 8)
    call(g, "SendMessage", (child,), 0x202, 0, 8 | 4 << 16)
    g.word(packet, 8, 9)
    call(g, "SendMessage", (child,), 0x100, 0x28, 0)
    g.word(packet, 1, 10)
    call(g, "SendMessage", (child,), 0x201, 1, 8 | 50 << 16)
    call(g, "SendMessage", (child,), 0x200, 1, 8 | 70 << 16)
    g.word(packet, 88 << 16 | 5, 11)
    call(g, "GetScrollPos", (child,), 2)
    g.result(50, 12)  # The owner decides whether to commit SB_THUMBTRACK.
    call(g, "SendMessage", (child,), 0x202, 0, 8 | 70 << 16)
    g.word(packet, 8, 13)
    g.word(count, 6, 14)
    call(g, "DestroyWindow", (parent,))
    call(g, "UnregisterClass", name, 0xFFFFFFFF)
    g.exit(0)
    procedure = 0x10000 + len(g.code)
    g.code.extend(b"\x81\x7c\x24\x08\x15\x01\0\0\x75")
    skip = len(g.code)
    g.code.extend(b"\0")
    g.code.extend(b"\xff\x05" + struct.pack("<I", count))
    for offset, destination in ((12, packet), (16, sender)):
        g.code.extend(b"\x8b\x44\x24" + bytes([offset]))
        store(g, destination)
    g.code[skip] = len(g.code) - skip - 1
    g.code.extend(b"\xb8\x01\0\0\0\xc3")
    g.data = g.data.replace(struct.pack("<I", 0x88776655), struct.pack("<I", procedure))
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "SCROLL32", data=g.data)
