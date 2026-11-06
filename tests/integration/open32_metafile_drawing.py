# Public domain. No rights reserved. Wholly AI-authored.
"""Mapped DIB pixels and five-argument WMF callbacks, including early stop."""

import struct

from images import FlatGuest, linear_image
from open32_contracts import store


def bitmap_metafile_program():
    g = FlatGuest()
    dc, bitmap, handle, calls, stop = (g.field(bytes(4)) for _ in range(5))
    point = g.field(struct.pack("<ii", 2, 2))
    previous = g.field(bytes(8))
    info_bytes = struct.pack("<IiiHH6I", 40, 2, 2, 1, 24, 0, 16, 0, 0, 0, 0)
    bits_bytes = bytes.fromhex("00ff00ffffff00000000ffff00000000")
    info, bits = g.field(info_bytes), g.field(bits_bytes)

    def record(function, parameters=b""):
        return struct.pack("<IH", (6 + len(parameters)) // 2, function) + parameters

    records = [
        record(0x107, struct.pack("<H", 8)),
        record(0x20B, struct.pack("<hh", 0, 0)),
        record(0x20C, struct.pack("<hh", 2, 2)),
        record(
            0xF43,
            struct.pack("<IH8h", 0xCC0020, 0, 2, 2, 0, 0, 2, 2, 0, 0) + info_bytes + bits_bytes,
        ),
        record(0),
    ]
    payload = b"".join(records)
    wmf = (
        struct.pack(
            "<HHHIHIH",
            1,
            9,
            0x300,
            (18 + len(payload)) // 2,
            0,
            max(len(r) for r in records) // 2,
            0,
        )
        + payload
    )
    g.call(2, 62, 0)
    store(g, dc)
    g.call(2, 327, (dc,))
    g.result(10, 1)
    g.call(2, 58, 16, 16, 1, 24, 0)  # CreateBitmap
    store(g, bitmap)
    g.call(2, 327, (bitmap,))
    g.result(7, 2)
    g.call(2, 585, (dc,), (bitmap,))  # SelectObject
    g.call(2, 630, len(wmf), g.field(wmf))
    store(g, handle)
    g.call(2, 327, (handle,))
    g.result(9, 3)
    g.call(2, 576, (dc,))  # SaveDC: outer state must survive WMF playback.
    g.result(1, 4)
    g.call(2, 626, (dc,), 8)
    g.result(1, 5)
    for ordinal, x, y in ((655, 5, 6), (654, 4, 4), (658, 2, 2)):
        g.call(2, ordinal, (dc,), x, y, previous)
        g.result(1, 6)
    g.call(2, 517, (dc,), (handle,))
    g.result(1, 7)
    g.call(2, 453, (dc,), point, 1)
    g.result(1, 8)
    g.word(point, 9, 9)
    g.word(point + 4, 10, 10)
    g.call(2, 100, (dc,), point, 1)
    g.result(1, 11)
    g.word(point, 2, 12)
    g.word(point + 4, 2, 13)
    g.call(2, 654, (dc,), 0, 4, previous)
    g.result(0, 26)
    g.call(2, 655, (dc,), 100, 100, 0xDEADBEEF)
    g.result(0, 27)
    g.call(2, 453, (dc,), point, 1)
    g.result(1, 28)
    g.word(point, 9, 29)
    g.word(point + 4, 10, 29)
    g.call(2, 181, (dc,), (handle,), 0x11223344, 0xAABBCCDD)
    g.result(1, 14)
    g.word(calls, 5, 15)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", calls, 0))
    g.code.extend(b"\xc7\x05" + struct.pack("<II", stop, 3))
    g.call(2, 181, (dc,), (handle,), 0x11223344, 0xAABBCCDD)
    g.result(0, 16)
    g.word(calls, 3, 17)
    g.call(2, 572, (dc,), 1)
    g.result(1, 18)
    for x, y, color in ((5, 6, 0xFF), (8, 6, 0xFF0000), (5, 9, 0xFF00), (8, 9, 0xFFFFFF)):
        g.call(2, 335, (dc,), x, y)  # GetPixel
        g.result(color, 19)
    # Raw StretchDIBits also supports top-down image headers and clipped output.
    top_info = g.field(info_bytes[:8] + struct.pack("<i", -2) + info_bytes[12:])
    g.call(2, 677, (dc,), 0, 0, 2, 2, 0, 0, 2, 2, bits, top_info, 0, 0xCC0020)
    g.result(2, 20)
    g.call(2, 335, (dc,), 0, 0)
    g.result(0xFF00, 21)
    # Opposing destination/source signs mirror the two columns.
    for x, width, sx, sw in ((2, 0xFFFFFFFE, 0, 2), (0, 2, 2, 0xFFFFFFFE)):
        g.call(2, 677, (dc,), x, 0, width, 2, sx, 0, sw, 2, bits, info, 0, 0xCC0020)
        g.result(2, 33)
        g.call(2, 335, (dc,), 0, 0)
        g.result(0xFF0000, 34)
    g.call(2, 677, (dc,), 0xFFFFFFFF, 0xFFFFFFFF, 2, 2, 0, 0, 2, 2, bits, info, 0, 0xCC0020)
    g.call(2, 335, (dc,), 0, 0)
    g.result(0xFFFFFF, 35)
    g.call(2, 677, (dc,), 0, 0, 2, 2, 0, 0, 2, 2, bits, info, 0, 0xCC0020)
    g.call(2, 677, (dc,), 0, 0, 2, 2, 0, 0, 2, 2, bits, info, 0, 0x660046)
    g.call(2, 335, (dc,), 0, 0)
    g.result(0, 36)
    g.call(2, 677, (dc,), 0, 0, 2, 2, 1, 0, 2, 2, bits, info, 0, 0xCC0020)
    g.result(0, 37)
    g.call(2, 677, (dc,), 0, 0, 2, 2, 0, 0, 2, 2, 0xDEADBEEF, info, 0, 0xCC0020)
    g.result(0, 38)
    g.call(2, 518, (dc,), 0, 0xDEADBEEF, 0)
    g.result(0, 22)
    g.call(2, 139, (handle,))
    g.result(1, 23)
    g.call(2, 135, (dc,))
    g.result(1, 24)
    g.call(2, 140, (bitmap,))
    g.result(1, 25)
    g.exit(0)
    callback = 0x10000 + len(g.code)
    g.require(b"\x81\x7c\x24\x14" + struct.pack("<I", 0xAABBCCDD), 30)
    g.require(b"\x83\x7c\x24\x10\x00", 31)
    # Preserve the callback's argument frame while pushing PlayMetaFileRecord args.
    g.code.extend(b"\x55\x89\xe5")
    for offset in (20, 16, 12, 8):
        g.code.extend(b"\xff\x75" + bytes([offset]))
    g.call(2, 518)
    g.code.extend(b"\x83\xc4\x10\x89\xec\x5d")
    g.result(1, 32)
    g.code.extend(b"\xff\x05" + struct.pack("<I", calls))
    g.load(calls)
    g.code.extend(b"\x3b\x05" + struct.pack("<I", stop) + b"\x0f\x95\xc0\x0f\xb6\xc0\xc3")
    g.code = g.code.replace(struct.pack("<I", 0x11223344), struct.pack("<I", callback))
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "WMFDRAW", data=g.data)
