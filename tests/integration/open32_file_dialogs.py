# Public domain. No rights reserved. Wholly AI-authored.
"""Common file dialogs exercise native templates, guest hooks and checked outputs."""

import struct

from images import FlatGuest, linear_image


def file_template():
    controls = [
        (258, 6, 5, 100, 150, 12, 0x20004, ""),
        (262, 2, 5, 70, 150, 40, 0x20004, ""),
        (264, 7, 5, 25, 70, 40, 0x20000, ""),
        (266, 7, 80, 25, 70, 40, 0x20000, ""),
        (269, 3, 5, 5, 50, 12, 2, "Read only"),
        (1, 3, 160, 100, 50, 12, 0x20400, "Save"),
        (2, 3, 160, 80, 50, 12, 0x20000, "Cancel"),
        (500, 3, 160, 60, 50, 12, 7, ""),
    ]
    data = bytearray(14 + (len(controls) + 1) * 30)
    struct.pack_into("<7H", data, 0, 0, 0, 437, 14, 1, 1, 0)
    struct.pack_into(
        "<6HI4h3H",
        data,
        14,
        0,
        len(controls),
        0,
        1,
        0,
        0,
        0x80000000,
        0,
        0,
        220,
        120,
        700,
        65535,
        65535,
    )
    for i, (identity, kind, x, y, width, height, style, text) in enumerate(controls):
        struct.pack_into(
            "<6HI4h3H",
            data,
            44 + i * 30,
            0,
            0,
            0,
            kind,
            len(text),
            len(data),
            0x80000000 | style,
            x,
            y,
            width,
            height,
            identity,
            65535,
            65535,
        )
        data.extend(text.encode() + b"\0")
    struct.pack_into("<H", data, 0, len(data))
    return data


def file_dialog_program(work, mode, checked):
    g = FlatGuest()
    window, initialized, painted, validated, file_ok = (g.field(bytes(4)) for _ in range(5))
    edit, previous = g.field(bytes(4)), g.field(bytes(4))
    characters = g.field(bytes(4))
    filename = g.field(bytes(260))
    title = g.field(bytes(64))
    filters = g.field(b"CSV\0*.csv\0Workbook\0*.123\0All files\0*.*\0\0")
    extension = g.field(b"123\0")
    directory = "C:" + str(work).replace("/", "\\")
    initial = g.field(directory.encode() + b"\0")
    name = "existing.123" if mode == "open" else "chosen"
    edited = g.field(name.encode() + b"\0")
    expected = (directory + "\\" + (name if mode == "open" else "chosen.123")).encode() + b"\0"
    record = g.field(
        struct.pack(
            "<19I",
            76,
            0,
            0,
            filters,
            0,
            0,
            2,
            filename,
            2 if mode == "short" else 260,
            title,
            64,
            initial,
            0,
            0x86C | (0x1000 if mode == "open" else 0),
            0,
            extension,
            0x12345678,
            0xABCDEF12,
            700,
        )
    )
    message_name = g.field(b"commdlg_FileNameOK\0")
    g.call(2, 558, message_name)  # RegisterWindowMessage
    g.code.extend(b"\xa3" + struct.pack("<I", file_ok))
    if checked:
        g.call(2, 350, 0xFFFFFFFF)
        g.result(0, 1)
        g.call(2, 48)
        g.result(1, 2)
    g.call(2, 329 if mode == "open" else 350, record)
    g.result(0 if mode in ("cancel", "veto", "short") else 1, 3)
    g.call(2, 48)
    g.result(0x3003 if mode == "short" else 0, 4)
    g.word(initialized, 1, 5)
    g.word(painted, 1, 6)
    if mode in ("save", "open", "veto"):
        g.word(validated, 1, 7)
        for offset in range(0, len(expected) - 3, 4):
            g.word(filename + offset, int.from_bytes(expected[offset : offset + 4], "little"), 8)
        g.word(record + 56, (len(directory) + 1) | (len(expected) - 4) << 16, 9)
        g.word(
            title, int.from_bytes(expected[len(directory) + 1 : len(directory) + 5], "little"), 10
        )
    if mode == "cancel":
        g.word(filename, 0, 11)
    if mode == "short":
        g.load(filename)
        g.require(b"\x25\xff\xff\0\0\x3d" + struct.pack("<I", len(expected)), 12)
    g.call(2, 444, 0x1234, 269)
    g.result(0, 13)
    g.exit(0)

    def branch_if_not(message):
        g.code.extend(b"\x81\x7c\x24\x08" + struct.pack("<I", message) + b"\x0f\x85")
        offset = len(g.code)
        g.code.extend(bytes(4))
        return offset

    def target(offset):
        struct.pack_into("<i", g.code, offset, len(g.code) - offset - 4)

    callback = 0x10000 + len(g.code)
    next_message = branch_if_not(0x110)
    g.code.extend(b"\x8b\x44\x24\x04\xa3" + struct.pack("<I", window))
    g.require(b"\x81\x7c\x24\x10" + struct.pack("<I", record), 20)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", initialized, 1))
    g.call(2, 611, (window,), 258, edited)
    g.result(1, 21)
    g.call(2, 32, (window,), 269, 1)
    g.result(1, 22)
    g.call(2, 444, (window,), 269)
    g.result(1, 23)
    g.call(2, 273, (window,), 258)
    g.code.extend(b"\xa3" + struct.pack("<I", edit))
    g.call(2, 659, (edit,), 0xFFFFFFEB, 0x12345678)
    g.result(0, 30)
    g.call(2, 385, (edit,), 0xFFFFFFEB)
    g.result(0x12345678, 31)
    g.call(2, 637, (edit,), 0x123, 0xBEEF)
    g.result(1, 32)
    g.call(2, 344, (edit,), 0x123)
    g.result(0xBEEF, 33)
    g.call(2, 566, (edit,), 0x123)
    g.result(0xBEEF, 34)
    g.call(2, 659, (edit,), 0xFFFFFFFC, 0x1234ABCD)
    g.code.extend(b"\xa3" + struct.pack("<I", previous))
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 35)
    g.call(2, 588, (edit,), 0x401, 7, 8)  # SendMessage
    g.result(0xCAFE, 36)
    g.call(3, 920, (edit,), 0x7A, 0x2C010005, ord("z"))  # Native WM_CHAR
    g.word(characters, 1, 38)
    g.call(2, 611, (window,), 258, edited)
    g.result(1, 37)
    g.call(3, 899, (window,), 500)
    button = g.field(bytes(4))
    g.code.extend(b"\xa3" + struct.pack("<I", button))
    g.call(3, 920, (button,), 0x23, 0, 0)
    g.call(3, 919, (window,), 0x20, 2 if mode == "cancel" else 1, 0)
    g.result(1, 24)
    g.code.extend(b"\x31\xc0\xc3")
    target(next_message)
    next_message = branch_if_not(0x2B)
    g.code.extend(b"\x8b\x44\x24\x10")
    for offset, value in [(0, 4), (4, 500)]:
        g.require(b"\x81\x78" + bytes([offset]) + struct.pack("<I", value), 25)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", painted, 1))
    g.code.extend(b"\xb8\x01\0\0\0\xc3")
    target(next_message)
    g.code.extend(b"\x8b\x44\x24\x08\x3b\x05" + struct.pack("<I", file_ok) + b"\x0f\x85")
    next_message = len(g.code)
    g.code.extend(bytes(4))
    g.require(b"\x81\x7c\x24\x10" + struct.pack("<I", record), 26)
    g.code.extend(b"\xc7\x05" + struct.pack("<II", validated, 1))
    if mode == "veto":
        g.call(3, 919, (window,), 0x20, 2, 0)
        g.code.extend(b"\xb8\x01\0\0\0\xc3")
    target(next_message)
    g.code.extend(b"\x31\xc0\xc3")
    struct.pack_into("<I", g.data, record - 0x20000 + 68, callback)
    subclass = 0x10000 + len(g.code)
    g.code.extend(b"\x55\x89\xe5\x81\x7d\x0c\x01\x04\0\0\x75\x07")
    g.code.extend(b"\xb8\xfe\xca\0\0\xc9\xc3")
    g.code.extend(b"\x81\x7d\x0c\x02\x01\0\0\x75\x06\xff\x05" + struct.pack("<I", characters))
    for offset in (20, 16, 12, 8):
        g.code.extend(bytes((0xFF, 0x75, offset)))
    g.call(2, 22, (previous,))
    g.code.extend(b"\x83\xc4\x10\xc9\xc3")
    g.code = g.code.replace(struct.pack("<I", 0x1234ABCD), struct.pack("<I", subclass))
    return linear_image(
        g.code,
        g.fixups,
        ["DOSCALLS", "PMWINX", "PMWIN"],
        "FILEDIALOG32",
        data=g.data,
        resources=[(4, 700, file_template())],
    )
