# Public domain. No rights reserved. Wholly AI-authored.
"""Private profiles preserve Open32's uppercase keys and native binary INI namespace."""

import struct

from images import FlatGuest, linear_image


def profile_program(checked):
    g = FlatGuest()
    section, other = g.field(b"General\0"), g.field(b"general\0")
    key, value = g.field(b"api\0"), g.field(b"written\0")
    lower, bare = g.field(b"key\0"), g.field(b"bare\0")
    filename, text_ini = g.field(b"windows-private.ini\0"), g.field(b"text.ini\0")
    fallback, output = g.field(b"fallback  \0"), g.field(bytes(128))
    profile = g.field(bytes(4))
    g.call(2, 718, section, key, value, filename)
    g.result(1, 1)
    g.call(3, 102, 0, filename)
    g.code.extend(b"\xa3" + struct.pack("<I", profile))
    upper = g.field(b"API\0")
    g.call(3, 115, (profile,), section, upper, fallback, output, 128)
    g.result(7, 2)
    g.word(output, int.from_bytes(b"writ", "little"), 3)
    g.call(3, 116, (profile,), section, lower, bare)
    g.result(1, 4)
    g.call(3, 103, (profile,))
    g.result(1, 5)
    g.call(2, 340, section, key, fallback, output, 128, filename)
    g.result(7, 6)
    g.word(output, int.from_bytes(b"writ", "little"), 7)
    g.call(2, 340, other, key, fallback, output, 128, filename)
    g.result(10, 8)
    g.word(output + 4, int.from_bytes(b"back", "little"), 9)
    g.call(2, 340, section, lower, fallback, output, 128, filename)
    g.result(10, 10)
    g.call(2, 340, section, key, fallback, output, 3, filename)
    g.result(2, 11)
    g.load(output)
    g.require(b"\x25\xff\xff\xff\0\x3d" + struct.pack("<I", int.from_bytes(b"wr\0", "little")), 12)
    g.call(2, 340, section, key, fallback, output, 128, text_ini)
    g.result(10, 13)
    g.call(2, 718, 0, 0, 0, filename)  # Flush retains all records.
    g.result(1, 14)
    g.call(2, 340, section, key, fallback, output, 128, filename)
    g.result(7, 15)
    g.call(2, 340, section, 0, fallback, output, 128, filename)
    g.result(8, 16)  # API\0key\0, plus the final terminator.
    g.call(2, 718, section, key, 0, filename)
    g.result(1, 17)
    g.call(2, 340, section, key, fallback, output, 128, filename)
    g.result(10, 18)
    if checked:
        g.call(2, 340, section, key, fallback, 0xFFFFFFFF, 128, filename)
        g.result(0, 19)
        g.call(2, 718, section, key, 0xFFFFFFFF, filename)
        g.result(0, 20)
    g.exit(0)
    return linear_image(
        g.code, g.fixups, ["DOSCALLS", "PMWINX", "PMSHAPI"], "PRIVATE32", data=g.data
    )
