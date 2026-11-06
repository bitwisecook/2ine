# Public domain. No rights reserved. Wholly AI-authored.
"""Text fitting returns cumulative advances and the whole string's dimensions."""

import struct

from images import FlatGuest, linear_image
from open32_contracts import store


def text_extent_program():
    g = FlatGuest()
    dc, fit, maximum = (g.field(bytes(4)) for _ in range(3))
    one, whole, size, advances = (g.field(bytes(16)) for _ in range(4))
    text = g.field(b"WWW")
    g.call(2, 62, 0)  # CreateCompatibleDC
    store(g, dc)
    g.call(2, 369, (dc,), text, 1, one)
    g.result(1, 1)
    g.call(2, 369, (dc,), text, 3, whole)
    g.result(1, 2)
    g.load(one)
    g.code.extend(b"\x01\xc0")
    store(g, maximum)
    g.call(2, 1021, (dc,), text, 3, (maximum,), fit, advances, size)
    g.result(1, 3)
    g.word(fit, 2, 4)
    for a, b in (
        (advances, one),
        (advances + 4, maximum),
        (advances + 8, whole),
        (size, whole),
        (size + 4, whole + 4),
    ):
        g.load(a)
        g.require(b"\x3b\x05" + struct.pack("<I", b), 5)
    # Null fit ignores the width limit; null advances is also supported.
    g.call(2, 1021, (dc,), text, 3, 0xFFFFFFFF, 0, 0, size)
    g.result(1, 6)
    g.load(size)
    g.require(b"\x3b\x05" + struct.pack("<I", whole), 7)
    g.call(2, 1021, (dc,), 0, 0, 0, fit, 0, size)
    g.result(1, 8)
    for address in (fit, size, size + 4):
        g.word(address, 0, 9)
    for count, source, fitted, dx, result in (
        (3, text, fit, advances, 0),
        (3, 0xDEADBEEF, fit, advances, size),
        (3, text, 0xDEADBEEF, advances, size),
        (3, text, fit, 0xDEADBEEF, size),
        (0xFFFFFFFF, text, fit, advances, size),
    ):
        g.call(2, 1021, (dc,), source, count, 100, fitted, dx, result)
        g.result(0, 10)
        g.call(2, 300)
        g.result(87, 11)
    g.call(2, 1021, (dc,), text, 3, 0xFFFFFFFF, fit, advances, size)
    g.result(0, 12)
    g.call(2, 1021, (dc,), text, 3, 0, fit, advances, size)
    g.result(1, 13)
    g.word(fit, 0, 14)
    tabs = g.field(struct.pack("<ii", 40, 80))
    for string, tab_count, extra in ((b"W\tW", 1, 40), (b"\t\tW", 2, 80)):
        g.call(2, 362, (dc,), g.field(string), len(string), tab_count, tabs)
        store(g, maximum)
        g.load(one + 4)
        g.code.extend(b"\xc1\xe0\x10\x03\x05" + struct.pack("<I", one))
        g.code.extend(b"\x05" + struct.pack("<I", extra))
        g.require(b"\x3b\x05" + struct.pack("<I", maximum), 16)
    g.call(2, 362, (dc,), 0, 0, 0, 0)
    g.result(0, 17)
    g.call(2, 362, (dc,), text, 3, 1, 0xDEADBEEF)
    g.result(0, 18)
    g.call(2, 300)
    g.result(87, 19)
    g.call(2, 135, (dc,))  # DeleteDC
    g.result(1, 15)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "TEXTFIT", data=g.data)
