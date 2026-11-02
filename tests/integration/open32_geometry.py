# Public domain. No rights reserved. Wholly AI-authored.
"""Window placements preserve normal geometry and validate the caller's record extent."""

import struct

from images import FlatGuest, linear_image


def placement_program(checked):
    g = FlatGuest()
    name = g.field(b"PlacementWindow\0")
    record = g.field(struct.pack("<10I", 0, 0xABCDEF12, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, name))
    placement = g.field(struct.pack("<11I", 44, *([0] * 10)))
    window = g.field(bytes(4))
    g.call(2, 556, record)
    g.call(2, 99, 0, name, 0, 0, 10, 20, 100, 50, 0, 0, 0xFFFFFFFF, 0)
    g.code.extend(b"\xa3" + struct.pack("<I", window))
    g.call(2, 387, (window,), placement)
    g.result(1, 1)
    g.word(placement + 8, 1, 2)
    for index, value in enumerate((10, 20, 110, 70)):
        g.word(placement + 28 + 4 * index, value, 3)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", placement, 43))
    g.call(2, 387, (window,), placement)
    g.result(0, 4)
    g.call(2, 300)
    g.result(87, 5)
    if checked:
        g.call(2, 387, (window,), 0xFFFFFFFF)
        g.result(0, 6)
    g.call(2, 146, (window,))
    g.result(1, 7)
    g.exit(0)
    callback = 0x10000 + len(g.code)
    g.code.extend(b"\x31\xc0\x81\x7c\x24\x08\x81\0\0\0\x0f\x94\xc0\xc3")
    g.data = g.data.replace(struct.pack("<I", 0xABCDEF12), struct.pack("<I", callback))
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "PLACEMENT32", data=g.data)
