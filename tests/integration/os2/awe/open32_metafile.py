# Public domain. No rights reserved. Wholly AI-authored.
"""Copied WMF ownership, round trips, bounded guest buffers and malformed records."""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from tests.integration.harness.images import FlatGuest, linear_image
from tests.integration.harness.open32_contracts import store


def metafile_program():
    g = FlatGuest()
    # SETBKCOLOR followed by EOF, both in the original 16-bit WMF wire format.
    wmf = struct.pack("<HHHIHIH", 1, 9, 0x300, 17, 0, 5, 0)
    wmf += struct.pack("<IHI", 5, 0x201, 0x123456) + struct.pack("<IH", 3, 0)
    source = g.field(wmf)
    output = g.field(bytes(40))
    handle = g.field(bytes(4))
    g.call(2, 630, len(wmf), source)
    store(g, handle)
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 1)
    # Reusing/freeing the application's original buffer cannot mutate the WMF.
    g.code.extend(b"\xc7\x05" + struct.pack("<II", source, 0))
    g.call(2, 316, (handle,), 0, 0)
    g.result(len(wmf), 2)
    g.call(2, 316, (handle,), 0xFFFFFFFF, output)
    g.result(len(wmf), 3)
    for offset in range(0, len(wmf) - 3, 4):
        g.word(output + offset, int.from_bytes(wmf[offset : offset + 4], "little"), 4)
    g.word(output + len(wmf), 0, 5)
    g.call(2, 316, (handle,), 4, output)
    g.result(4, 6)
    g.call(2, 316, (handle,), 4, 0xDEADBEEF)
    g.result(0, 7)
    g.call(2, 300)
    g.result(87, 8)
    g.call(2, 139, (handle,))
    g.result(1, 9)
    g.call(2, 316, (handle,), 0, 0)
    g.result(0, 10)
    g.call(2, 139, (handle,))
    g.result(0, 11)
    g.call(2, 300)
    g.result(6, 12)
    g.call(2, 630, 24, 0xDEADBEEF)
    g.result(0, 13)
    g.call(2, 630, 0xFFFFFFFF, source)
    g.result(0, 14)
    for offset, replacement in ((2, b"\x08\0"), (6, bytes(4)), (18, bytes(4)), (32, b"\x01\0")):
        invalid = bytearray(wmf)
        invalid[offset : offset + len(replacement)] = replacement
        g.call(2, 630, len(invalid), g.field(invalid))
        g.result(0, 15)
    # EOF-only WMF is valid; leave it for native-module shutdown to release.
    empty = struct.pack("<HHHIHIHIH", 1, 9, 0x100, 12, 0, 3, 0, 3, 0)
    g.call(2, 630, len(empty), g.field(empty))
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 16)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "WMFBITS", data=g.data)
