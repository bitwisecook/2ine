# Public domain. No rights reserved. Wholly AI-authored.
"""OS/2 ordinal icon controls retain color XOR pixels and their AND mask."""

import struct

from dialogs import template
from images import FlatGuest, linear_image
from open32_contracts import call, store


def icon_program():
    g = FlatGuest()
    window, child, dc, image, x, y = (g.field(bytes(4)) for _ in range(6))
    bounds = g.field(bytes(16))
    g.call(3, 763, 0)
    g.call(3, 716, 1, 0)
    g.call(3, 924, 1, 0, 0, 0, 777, 0)  # WinLoadDlg
    store(g, window)
    g.require(b"\x85\xc0\x0f\x95\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 1)
    g.call(3, 899, (window,), 1)
    store(g, child)
    g.call(3, 920, (child,), 0x101, 0, 0)  # SM_QUERYHANDLE
    store(g, image)
    g.call(3, 883, (window,), 1)
    g.call(3, 920, (child,), 0x23, 0, 0)  # WM_PAINT
    g.call(3, 840, (child,), bounds)
    g.result(1, 2)
    for source, destination in ((bounds + 8, x), (bounds + 12, y)):
        g.load(source)
        g.code.extend(b"\xd1\xe8\x48")
        store(g, destination)
    call(g, "GetDC", (child,))
    store(g, dc)
    for dx, dy, color in ((0, 0, 0x333333), (1, 0, 0xFF0000), (0, 1, 0xFF), (1, 1, 0xCCCCCC)):
        # The four cases visit the 2x2 image clockwise across the two rows.
        if dx == 1:
            g.code.extend(b"\xff\x05" + struct.pack("<I", x))
        elif dy == 1:
            g.code.extend(b"\xff\x0d" + struct.pack("<I", x))
            g.code.extend(b"\xff\x05" + struct.pack("<I", y))
        call(g, "GetPixel", (dc,), (x,), (y,))
        g.result(color, 3)
    call(g, "ReleaseDC", (child,), (dc,))
    g.call(3, 728, (window,))
    g.result(1, 4)
    g.call(3, 727, (image,))  # WinDestroyPointer: control released its owned resource.
    g.result(0, 5)
    g.exit(0)
    dialog = template()
    struct.pack_into(
        "<6HI4h3H", dialog, 44, 0, 0, 0, 5, 3, 108, 0x80000003, 2, 2, 40, 12, 1, 65535, 65535
    )
    dialog[108:111] = b"\xff\x2c\x01"
    # Color icon: mono fallback/mask header, color header, masks, then BGR rows.
    icon = struct.pack("<2sIhhI", b"CI", 26, 0, 0, 58)
    icon += struct.pack("<I4H", 12, 2, 4, 1, 1) + bytes.fromhex("000000ffffff")
    icon += struct.pack("<2sIhhI", b"CI", 26, 0, 0, 74)
    icon += struct.pack("<I4H", 12, 2, 2, 1, 24)
    icon += bytes(8) + bytes.fromhex("4000000080000000")
    icon += bytes.fromhex("0000ff0000000000ffffffff00000000")
    return linear_image(
        g.code,
        g.fixups,
        ["DOSCALLS", "PMWINX", "PMWIN"],
        "ICONS32",
        data=g.data,
        resources=[(4, 777, dialog), (1, 300, icon)],
    )
