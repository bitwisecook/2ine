# Public domain. No rights reserved. Wholly AI-authored.
"""Restore persisted Open32 WINDOWPLACEMENT records through the actual export."""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from tests.integration.harness.images import FlatGuest, linear_image


def set_placement_program(checked, mode="frame"):
    g = FlatGuest()
    name = g.field(b"SavedPlacementWindow\0")
    registration = g.field(struct.pack("<10I", 0, 0xABCDEF12, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, name))
    window, mdi, document, native = (g.field(bytes(4)) for _ in range(4))
    output = g.field(struct.pack("<11I", 44, *([0] * 10)))
    rect = g.field(bytes(16))
    normal = (60, 70, 360, 270)
    zoomed = (80, 90, 440, 330)

    def record(command, rectangle=normal, flags=0, length=44):
        return g.field(struct.pack("<11i", length, flags, command, -1, -1, -1, -1, *rectangle))

    def apply(handle, source):
        g.call(2, 661, (handle,), source)
        g.result(1, 1)

    def check(handle, state, rectangle):
        g.call(2, 387, (handle,), output)
        g.result(1, 2)
        g.word(output + 8, state, 3)
        for i, value in enumerate(rectangle):
            g.word(output + 28 + i * 4, value & 0xFFFFFFFF, 4 + i)

    g.call(2, 556, registration)
    g.call(2, 99, 0, name, name, 0xCF0000, 10, 20, 300, 200, 0, 0, 0xFFFFFFFF, 0)
    g.code.extend(b"\xa3" + struct.pack("<I", window))
    saved = record(1)
    apply(window, saved)
    check(window, 1, normal)
    if mode == "frame":
        g.word(saved, 44, 8)
        g.call(2, 388, (window,), rect)
        g.result(1, 9)
        for i, value in enumerate(normal):
            g.word(rect + i * 4, value, 10 + i)
        zoomed = (80, 90, 440, 330)
        apply(window, record(3, zoomed))
        g.call(2, 451, (window,))
        g.result(1, 14)
        check(window, 3, zoomed)
        g.call(2, 671, (window,), 9)
        check(window, 1, zoomed)
        minimized = (40, 50, 280, 230)
        apply(window, record(2, minimized))
        g.call(2, 445, (window,))
        g.result(1, 15)
        check(window, 2, minimized)
        apply(window, saved)
        g.call(2, 445, (window,))
        g.result(0, 16)
        check(window, 1, normal)
    if mode == "invalid":
        # Invalid records fail before changing the saved placement or window state.
        for source in (
            record(1, length=43),
            record(12),
            record(1, (20, 20, 10, 40)),
            record(1, (-2147483648, 0, 2147483647, 100)),
            record(1, flags=8),
        ):
            g.call(2, 661, (window,), source)
            g.result(0, 17)
            g.call(2, 300)
            g.result(87, 18)
        if checked:
            for address in (0, 0xFFFFFFFF):
                g.call(2, 661, (window,), address)
                g.result(0, 19)
                g.call(2, 300)
                g.result(87, 20)
        check(window, 1, normal)
    if mode == "native":
        # Native frames use the same top-left rectangle and restore geometry.
        flags = g.field(bytes(4))
        g.call(3, 908, 1, 0, flags, 0, name, 0, 0, 0, 0)
        g.code.extend(b"\xa3" + struct.pack("<I", native))
        apply(native, record(3, zoomed))
        check(native, 3, zoomed)
        apply(native, saved)
        check(native, 1, normal)
        g.call(3, 728, (native,))
        g.result(1, 21)
    if mode == "mdi":
        # Maximized MDI children keep a separate normal rectangle for restoration.
        mdi_name = g.field(b"MDICLIENT\0")
        g.call(2, 99, 0, mdi_name, 0, 0x50000000, 0, 0, 200, 100, (window,), 34, 0xFFFFFFFF, 0)
        g.code.extend(b"\xa3" + struct.pack("<I", mdi))
        g.call(2, 99, 0x40, name, name, 0x50CF0000, 3, 2, 12, 6, (mdi,), 77, 0xFFFFFFFF, 0)
        g.code.extend(b"\xa3" + struct.pack("<I", document))
        child_normal = (4, 5, 34, 25)
        apply(document, record(3, child_normal))
        check(document, 3, child_normal)
        g.call(2, 244, (document,), rect)
        g.result(1, 22)
        g.word(rect + 8, 200, 23)
        g.word(rect + 12, 100, 24)
        g.call(2, 671, (document,), 9)
        check(document, 1, child_normal)
        g.call(2, 244, (document,), rect)
        g.result(1, 25)
        g.word(rect + 8, 30, 26)
        g.word(rect + 12, 20, 27)
    g.call(2, 146, (window,))
    g.result(1, 28)
    g.exit(0)
    callback = 0x10000 + len(g.code)
    g.code.extend(b"\x31\xc0\x81\x7c\x24\x08\x81\0\0\0\x0f\x94\xc0\xc3")
    g.data = g.data.replace(struct.pack("<I", 0xABCDEF12), struct.pack("<I", callback))
    return linear_image(
        g.code, g.fixups, ["DOSCALLS", "PMWINX", "PMWIN"], "SETPLACEMENT", data=g.data
    )
