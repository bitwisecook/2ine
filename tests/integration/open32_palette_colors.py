# Public domain. No rights reserved. Wholly AI-authored.
"""Palette-relative RGB pens and brushes on a true-color drawable."""

import struct

from images import FlatGuest, linear_image


def palette_relative_program():
    g = FlatGuest()
    screen, dc, bitmap, old_bitmap, brush, pen, old_pen = (g.field(bytes(4)) for _ in range(7))
    rect = g.field(struct.pack("<4i", 0, 0, 4, 4))
    for ordinal, output, arguments in (
        (263, screen, (0,)),
        (62, dc, ((screen,),)),
        (61, bitmap, ((screen,), 4, 4)),
        (585, old_bitmap, ((dc,), (bitmap,))),
        (97, brush, (0x02CC8855,)),
        (87, pen, (0, 1, 0x02FF0000)),
        (585, old_pen, ((dc,), (pen,))),
    ):
        g.call(2, ordinal, *arguments)
        g.code.extend(b"\xa3" + struct.pack("<I", output))
        g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 1)
    g.call(2, 207, (dc,), rect, (brush,))
    g.result(1, 2)
    g.call(2, 335, (dc,), 2, 2)
    g.result(0x00CC8855, 3)
    g.call(2, 320, (dc,), 0x02CC8855)
    g.result(0x00CC8855, 21)
    g.call(2, 491, (dc,), 0, 0, 0)
    g.result(1, 4)
    g.call(2, 456, (dc,), 4, 0)
    g.result(1, 5)
    g.call(2, 335, (dc,), 2, 0)
    g.result(0x00FF0000, 6)
    mask, mask_dc, old_mask = (g.field(bytes(4)) for _ in range(3))
    key_rect = g.field(struct.pack("<4i", 0, 0, 4, 1))
    g.call(2, 593, (dc,), 0x02CC8855)
    g.result(0xFFFFFF, 10)
    g.call(2, 62, (screen,))
    g.code.extend(b"\xa3" + struct.pack("<I", mask_dc))
    g.call(2, 58, 4, 1, 1, 1, 0)
    g.code.extend(b"\xa3" + struct.pack("<I", mask))
    g.call(2, 585, (mask_dc,), (mask,))
    g.code.extend(b"\xa3" + struct.pack("<I", old_mask))
    g.call(2, 320, (mask_dc,), 0xEEEEEE)
    g.result(0xFFFFFF, 22)
    g.call(2, 320, (mask_dc,), 0x111111)
    g.result(0, 23)
    g.call(2, 320, 0xDEADBEEF, 0)
    g.result(0xFFFFFFFF, 24)
    # A colored line differs from the source background and must become a
    # black mask bit; the untouched second row must become white mask bits.
    g.call(2, 18, (mask_dc,), 0, 0, 4, 1, (dc,), 0, 0, 0xCC0020)
    g.result(1, 11)
    g.call(2, 335, (mask_dc,), 2, 0)
    g.result(0, 12)
    g.call(2, 18, (mask_dc,), 0, 0, 4, 1, (dc,), 0, 1, 0xCC0020)
    g.result(1, 13)
    g.call(2, 335, (mask_dc,), 2, 0)
    g.result(0xFFFFFF, 14)
    # Expanding white mask bits uses the destination background color.
    g.call(2, 593, (dc,), 0x02FF8800)
    g.result(0x02CC8855, 15)
    g.call(2, 18, (dc,), 0, 0, 4, 1, (mask_dc,), 0, 0, 0xCC0020)
    g.result(1, 16)
    g.call(2, 335, (dc,), 2, 0)
    g.result(0x00FF8800, 17)
    g.call(2, 649, (dc,), 0x02001122)
    g.result(0, 18)
    black = g.field(bytes(4))
    g.call(2, 97, 0)
    g.code.extend(b"\xa3" + struct.pack("<I", black))
    g.call(2, 207, (mask_dc,), key_rect, (black,))
    g.call(2, 140, (black,))
    g.call(2, 18, (dc,), 0, 0, 4, 1, (mask_dc,), 0, 0, 0xCC0020)
    g.result(1, 19)
    g.call(2, 335, (dc,), 2, 0)
    g.result(0x001122, 20)
    g.call(2, 585, (mask_dc,), (old_mask,))
    g.call(2, 140, (mask,))
    g.call(2, 135, (mask_dc,))
    g.call(2, 585, (dc,), (old_pen,))
    g.call(2, 585, (dc,), (old_bitmap,))
    for handle in (brush, pen, bitmap):
        g.call(2, 140, (handle,))
        g.result(1, 7)
    g.call(2, 135, (dc,))
    g.result(1, 8)
    g.call(2, 560, 0, (screen,))
    g.result(1, 9)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "PALETTERGB", data=g.data)
