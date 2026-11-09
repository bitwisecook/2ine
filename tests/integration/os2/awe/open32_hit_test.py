# Public domain. No rights reserved. Wholly AI-authored.
"""Screen-pixel hit testing skips disabled/hidden children and hides PM clients."""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from tests.integration.harness.images import FlatGuest, linear_image
from tests.integration.harness.open32_contracts import store


def hit_test_program():
    g = FlatGuest()
    name = g.field(b"HitTestWindow\0")
    parent, child, other = (g.field(bytes(4)) for _ in range(3))
    point = g.field(bytes(8))
    record = g.field(struct.pack("<10I", 0, 0x11223344, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, name))
    g.call(2, 556, record)
    g.call(2, 99, 0, name, name, 0x90000000, 100, 100, 200, 100, 0, 0, 0xFFFFFFFF, 0)
    store(g, parent)
    for output in (child, other):
        g.call(2, 99, 0, name, name, 0x50000000, 10, 10, 50, 30, (parent,), 0, 0xFFFFFFFF, 0)
        store(g, output)
    g.call(2, 716, 115, 115)
    g.require(b"\x3b\x05" + struct.pack("<I", other), 1)
    g.call(2, 168, (other,), 0)  # EnableWindow
    g.call(2, 716, 115, 115)
    g.require(b"\x3b\x05" + struct.pack("<I", child), 2)
    g.call(2, 671, (child,), 0)  # ShowWindow(SW_HIDE)
    g.call(2, 716, 115, 115)
    g.require(b"\x3b\x05" + struct.pack("<I", parent), 3)
    g.call(2, 716, 0xFFFFFFFF, 0)
    g.result(0, 4)
    g.call(2, 262, point)
    g.result(1, 5)
    g.call(2, 262, 0xDEADBEEF)
    g.result(0, 6)
    g.call(2, 300)
    g.result(87, 7)
    g.call(2, 146, (parent,))
    g.exit(0)
    procedure = 0x10000 + len(g.code)
    g.code.extend(b"\x31\xc0\x81\x7c\x24\x08\x81\0\0\0\x0f\x94\xc0\xc3")
    g.data = g.data.replace(struct.pack("<I", 0x11223344), struct.pack("<I", procedure))
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "HITTEST", data=g.data)
