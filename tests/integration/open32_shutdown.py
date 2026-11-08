# Public domain. No rights reserved. Wholly AI-authored.
"""System menu commands reach Windows callbacks and preserve close vetoes."""

import struct

from images import FlatGuest, linear_image
from open32_contracts import call, store


def system_command_program():
    g = FlatGuest()
    name = g.field(b"SystemCommandContract\0")
    window, client, close_count, command = (g.field(bytes(4)) for _ in range(4))
    record = g.field(struct.pack("<10I", 0, 0x88776655, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, name))
    call(g, "RegisterClass", record)
    call(g, "CreateWindowEx", 0, name, name, 0xCF0000, 10, 20, 120, 80, 0, 0, 0xFFFFFFFF, 0)
    store(g, window)
    g.call(3, 899, (window,), 0x8008)  # Native FID_CLIENT
    store(g, client)
    # Open32 system menus post Windows IDs to the native frame. The callback
    # must see SC_CLOSE, and its WM_CLOSE handler can veto destruction.
    g.call(3, 920, (window,), 0x21, 0xF060, 0)
    g.word(command, 0xF060, 1)
    g.word(close_count, 1, 2)
    call(g, "IsWindow", (window,))
    g.result(1, 3)
    # Native PM commands forwarded directly to the client use PM IDs.
    g.call(3, 920, (client,), 0x21, 0x8004, 0)
    g.word(command, 0xF060, 4)
    g.word(close_count, 2, 5)
    # DefWindowProc masks the reserved low bits of Windows SC_* IDs.
    g.call(2, 131, (window,), 0x112, 0xF061, 0)
    g.word(close_count, 3, 6)
    call(g, "IsWindow", (window,))
    g.result(1, 7)
    # Maximise and restore through the same system-menu/default path.
    placement = g.field(struct.pack("<11I", 44, *([0] * 10)))
    for windows_id, show_command in ((0xF030, 3), (0xF120, 1)):
        g.call(3, 920, (window,), 0x21, windows_id, 0)
        g.call(2, 387, (window,), placement)
        g.result(1, 8)
        g.word(placement + 8, show_command, 9)
    # Native frame events must return from GetMessage before the application's
    # callback runs. Otherwise deferred Exit work is trapped inside GetMessage.
    message = g.field(bytes(28))
    before, elapsed = g.field(bytes(4)), g.field(bytes(4))
    for native_id, windows_id, first in ((0x20, 0x111, 0x805C), (0x29, 16, 0)):
        g.call(2, 373)  # GetTickCount
        store(g, before)
        g.call(3, 919, (window,), native_id, first, 0)
        g.result(1, 18)
        call(g, "GetMessage", message, 0, 0, 0)
        g.result(1, 19)
        g.word(message + 4, windows_id, 20)
        g.word(message + 8, first, 21)
        g.word(close_count, 3, 22)
        g.call(2, 373)
        g.code.extend(b"\x2b\x05" + struct.pack("<I", before))
        store(g, elapsed)
        call(g, "GetMessageTime")
        g.code.extend(b"\x2b\x05" + struct.pack("<I", before))
        # Unsigned subtraction also accepts a tick-count wrap in this interval.
        g.require(
            b"\x3b\x05" + struct.pack("<I", elapsed) + b"\x0f\x96\xc0\x0f\xb6\xc0\x3d\x01\0\0\0", 24
        )
        call(g, "DispatchMessage", message)
    g.word(close_count, 4, 23)
    # Mixed native/Windows loops can also consume same-thread Open32 posts,
    # including PostQuitMessage's exit status.
    native = g.field(bytes(32))
    call(g, "PostMessage", (window,), 16, 0, 0)
    g.result(1, 11)
    g.call(3, 915, 1, native, 0, 0, 0)  # WinGetMsg
    g.result(1, 12)
    g.call(2, 6007, native)  # WinXProcessMsg
    g.result(1, 13)
    g.word(close_count, 5, 14)
    call(g, "PostQuitMessage", 27)
    g.call(3, 915, 1, native, 0, 0, 0)
    g.result(0, 15)
    g.word(native + 4, 0x2A, 16)  # WM_QUIT
    g.word(native + 8, 27, 17)
    call(g, "DestroyWindow", (window,))
    g.result(1, 10)
    g.exit(0)

    callback = 0x10000 + len(g.code)
    g.code.extend(b"\x81\x7c\x24\x08\x12\x01\0\0\x0f\x85")
    other = len(g.code)
    g.code.extend(bytes(4))
    g.code.extend(b"\x8b\x44\x24\x0c")
    store(g, command)
    g.call(2, 131, (window,), 0x112, (command,), 0)
    g.code.extend(b"\xc3")
    g.code[other : other + 4] = struct.pack("<i", len(g.code) - other - 4)
    g.code.extend(b"\x83\x7c\x24\x08\x10\x75\x06\xff\x05" + struct.pack("<I", close_count))
    g.code.extend(b"\xb8\x01\0\0\0\xc3")
    g.data = g.data.replace(struct.pack("<I", 0x88776655), struct.pack("<I", callback))
    return linear_image(
        g.code, g.fixups, ["DOSCALLS", "PMWINX", "PMWIN"], "SYSTEMCOMMAND", data=g.data
    )


def paint_filter_program():
    g = FlatGuest()
    name = g.field(b"FilteredPaintContract\0")
    first, second, painted = (g.field(bytes(4)) for _ in range(3))
    message = g.field(bytes(28))
    record = g.field(struct.pack("<10I", 0, 0x88776655, 0, 0, 0xFFFFFFFF, 0, 0, 0, 0, name))
    call(g, "RegisterClass", record)
    for target, x in ((first, 0), (second, 120)):
        call(g, "CreateWindowEx", 0, name, name, 0x10000000, x, 0, 100, 50, 0, 0, 0xFFFFFFFF, 0)
        store(g, target)
        call(g, "InvalidateRect", (target,), 0, 0)
        g.result(1, 1)
    # The application checks for quit while both windows still need painting.
    call(g, "PeekMessage", message, 0, 18, 18, 0)
    g.result(0, 2)
    g.call(2, 376, (first,), 0, 0)  # GetUpdateRect
    g.result(1, 17)
    call(g, "PeekMessage", message, (first,), 15, 15, 0)
    g.result(1, 3)
    g.load(first)
    g.require(b"\x3b\x05" + struct.pack("<I", message), 4)
    # That paint is queued but not removed or validated. A different filter
    # must still return promptly and leave the queued paint available.
    call(g, "PeekMessage", message, 0, 18, 18, 0)
    g.result(0, 5)
    call(g, "PeekMessage", message, (second,), 15, 15, 1)
    g.result(1, 6)
    g.load(second)
    g.require(b"\x3b\x05" + struct.pack("<I", message), 7)
    # A blocking filtered retrieval must receive its timer behind deferred
    # paint, without spinning or dispatching either application's WM_PAINT.
    g.call(3, 884, 1, (second,), 71, 2)
    g.result(71, 8)
    call(g, "GetMessage", message, (second,), 0x113, 0x113)
    g.result(1, 9)
    g.word(message + 4, 0x113, 10)
    g.word(message + 8, 71, 11)
    g.call(3, 885, 1, (second,), 71)
    g.result(1, 12)
    for target in (first, second):
        g.call(2, 376, (target,), 0, 0)
        g.result(1, 18)
    call(g, "PeekMessage", message, (first,), 15, 15, 1)
    g.result(1, 13)
    g.word(painted, 0, 14)
    for target in (first, second):
        call(g, "ValidateRect", (target,), 0)
        g.result(1, 15)
        call(g, "DestroyWindow", (target,))
        g.result(1, 16)
    g.exit(0)
    callback = 0x10000 + len(g.code)
    g.code.extend(b"\x83\x7c\x24\x08\x0f\x75\x06\xff\x05" + struct.pack("<I", painted))
    g.code.extend(b"\xb8\x01\0\0\0\xc3")
    g.data = g.data.replace(struct.pack("<I", 0x88776655), struct.pack("<I", callback))
    return linear_image(
        g.code, g.fixups, ["DOSCALLS", "PMWINX", "PMWIN"], "FILTEREDPAINT", data=g.data
    )
