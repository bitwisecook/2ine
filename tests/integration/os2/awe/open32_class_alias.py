# Public domain. No rights reserved. Wholly AI-authored.
"""Registered-class aliases use names and preserve separate class metadata."""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from tests.integration.harness.images import FlatGuest, linear_image
from tests.integration.harness.open32_contracts import call, store


def class_alias_program():
    g = FlatGuest()
    library, procedure = g.field(b"PMWINX\0"), g.field(b"DEFWINDOWPROC\0")
    name, alias, missing = (
        g.field(text + b"\0") for text in [b"AliasSource", b"AliasTarget", b"NoSuchClass"]
    )
    module, window, output = g.field(bytes(4)), g.field(bytes(4)), g.field(bytes(40))
    records = [
        g.field(struct.pack("<10I", 0, 0, 0, extra, 0xFFFFFFFF, 0, 0, 0, 0, title))
        for title, extra in [(name, 8), (alias, 16)]
    ]
    call(g, "GetModuleHandle", library)
    store(g, module)
    call(g, "GetProcAddress", (module,), procedure)
    for record in records:
        store(g, record + 4)
    for record in records:
        call(g, "RegisterClass", record)
        g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 1)
    for first, second in [(name, alias), (alias, name), (name, name), (name, alias)]:
        g.call(2, 729, first, second)
        g.result(1, 2)
    for first, second in [
        (name, missing),
        (missing, name),
        (0, name),
        (name, 0),
        (0xFFFF0000, name),
    ]:
        g.call(2, 729, first, second)
        g.result(0, 3)
        call(g, "GetLastError")
        g.result(87, 4)
    g.call(2, 240, 0xFFFFFFFF, name, output)
    g.result(1, 5)
    g.word(output + 12, 8, 6)
    g.call(2, 240, 0xFFFFFFFF, alias, output)
    g.result(1, 7)
    g.word(output + 12, 16, 8)
    call(g, "UnregisterClass", alias, 0xFFFFFFFF)
    g.result(1, 9)
    g.call(2, 729, name, alias)
    g.result(0, 10)
    call(g, "CreateWindowEx", 0, name, name, 0, 0, 0, 80, 40, 0, 0, 0xFFFFFFFF, 0)
    store(g, window)
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 11)
    call(g, "DestroyWindow", (window,))
    call(g, "UnregisterClass", name, 0xFFFFFFFF)
    g.result(1, 12)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "CLASSALIAS", data=g.data)


def class_long_program():
    g = FlatGuest()
    library, proc_name = g.field(b"PMWINX\0"), g.field(b"DEFWINDOWPROC\0")
    name = g.field(b"ClassLongContract\0")
    module, old_proc, first, second = (g.field(bytes(4)) for _ in range(4))
    record = g.field(struct.pack("<10I", 0, 0, 8, 0, 0xFFFFFFFF, 0, 0, 0, 0, name))
    call(g, "GetModuleHandle", library)
    store(g, module)
    call(g, "GetProcAddress", (module,), proc_name)
    store(g, old_proc)
    store(g, record + 4)
    call(g, "RegisterClass", record)
    call(g, "CreateWindowEx", 0, name, name, 0, 0, 0, 80, 40, 0, 0, 0xFFFFFFFF, 0)
    store(g, first)
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 1)
    for index, value in [
        (2, 0x12345678),
        (-14, 0x654321),
        (-34, 0x1234),
        (-12, 17),
        (-10, 13),
        (-26, 8),
        (-8, 55),
    ]:
        g.call(2, 600, (first,), index & 0xFFFFFFFF, value)
        g.result(0, 2)
        g.call(2, 241, (first,), index & 0xFFFFFFFF)
        g.result(value, 3)
    g.call(2, 600, (first,), 5, 0xDEADBEEF)
    g.result(0, 4)
    call(g, "GetLastError")
    g.result(1413, 5)
    g.call(2, 241, (first,), 2)
    g.result(0x12345678, 6)
    g.call(2, 600, (first,), 0xFFFFFFE8, 0x88776655)
    g.require(b"\x3b\x05" + struct.pack("<I", old_proc), 7)
    g.call(2, 385, (first,), 0xFFFFFFFC)
    g.require(b"\x3b\x05" + struct.pack("<I", old_proc), 8)
    # Menu resource55 deliberately has no module: remove it before next creation.
    g.call(2, 600, (first,), 0xFFFFFFF8, 0)
    g.result(55, 9)
    call(g, "CreateWindowEx", 0, name, name, 0, 0, 0, 80, 40, 0, 0, 0xFFFFFFFF, 0)
    store(g, second)
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 10)
    g.call(2, 385, (second,), 0xFFFFFFFC)
    g.result(0x88776655, 11)
    g.call(2, 600, (first,), 0xFFFFFFE8, 0xDEADBEEF)
    g.result(0, 12)
    call(g, "GetLastError")
    g.result(87, 13)
    g.call(2, 241, (second,), 0xFFFFFFE8)
    g.result(0x88776655, 14)
    call(g, "DestroyWindow", (first,))
    call(g, "DestroyWindow", (second,))
    call(g, "UnregisterClass", name, 0xFFFFFFFF)
    g.result(1, 15)
    g.exit(0)
    callback = 0x10000 + len(g.code)
    g.code.extend(b"\x31\xc0\x81\x7c\x24\x08\x81\0\0\0\x0f\x94\xc0\xc3")
    g.code = g.code.replace(struct.pack("<I", 0x88776655), struct.pack("<I", callback))
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "CLASSLONG", data=g.data)
