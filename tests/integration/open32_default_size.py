# Public domain. No rights reserved. Wholly AI-authored.
"""Default width ignores the caller's height when creating overlapped windows."""

import struct

from images import FlatGuest, linear_image
from open32_contracts import call, store


def default_size_program():
    g = FlatGuest()
    library, procedure = g.field(b"PMWINX\0"), g.field(b"DEFWINDOWPROC\0")
    name, module, window, rectangle = (
        g.field(b"DefaultSize\0"),
        g.field(bytes(4)),
        g.field(bytes(4)),
        g.field(bytes(16)),
    )
    record = g.field(struct.pack("<10I", 0, 0, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, name))
    call(g, "GetModuleHandle", library)
    store(g, module)
    call(g, "GetProcAddress", (module,), procedure)
    store(g, record + 4)
    call(g, "RegisterClass", record)
    for style, height in [
        (0x02CF0000, 2147483638),
        (0x00CF0000, 7),
        (0x00CF0000, 0x80000000),
        (0x80000000, 2147483638),
    ]:
        call(
            g, "CreateWindowEx", 0, name, name, style, 0, 0, 0x80000000, height, 0, 0, 0xFFFFFFFF, 0
        )
        store(g, window)
        g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 1)
        call(g, "GetWindowRect", (window,), rectangle)
        g.result(1, 2)
        for index, value in enumerate((0, 0, 0, 0) if style == 0x80000000 else (0, 0, 640, 480)):
            g.word(rectangle + 4 * index, value, 3 + index)
        call(g, "DestroyWindow", (window,))
        g.result(1, 7)
    call(g, "UnregisterClass", name, 0xFFFFFFFF)
    g.result(1, 8)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "DEFAULTSIZE", data=g.data)
