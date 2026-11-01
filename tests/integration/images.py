# Public domain. No rights reserved. Wholly AI-authored.
"""Small LX images and caller-cleaned x86 guests for executable API contracts."""

import struct


def linear_image(code, fixups, modules, name, library=False, data=b"", resources=(), exports=()):
    header = bytearray(196)
    header[:2] = b"LX"
    struct.pack_into("<HH", header, 8, 2, 1)
    objects = [(4096, 0x10000, 0x2005, 1, 1, 0)]
    objects.append((65536, 0x20000, 0x2003, 2, int(bool(data)), 0))
    pages = [code]
    if data:
        pages.append(data)
    resource_bytes = bytearray()
    resource_table = bytearray()
    for kind, identity, payload in resources:
        resource_table.extend(
            struct.pack("<HHIHI", kind, identity, len(payload), 3, len(resource_bytes))
        )
        resource_bytes.extend(payload)
    if resources:
        # Resource objects in EXEs have no fixed load address.
        objects.append((len(resource_bytes), 0, 9, len(pages) + 1, 1, 0))
        pages.append(resource_bytes)
    assert all(len(page) <= 4096 for page in pages)
    for offset, value in {
        16: 0x8000 if library else 0,
        20: len(pages),
        24: 1,
        32: 2,
        36: 0xFFF0,
        40: 4096,
        64: 196,
        68: len(objects),
        72: 196 + len(objects) * 24,
        148: 2,
    }.items():
        struct.pack_into("<I", header, offset, value)
    tables = bytearray()
    for obj in objects:
        tables.extend(struct.pack("<IIIIII", *obj))
    for i, page in enumerate(pages):
        tables.extend(struct.pack("<IHH", i * 4096, len(page), 0))
    if resources:
        struct.pack_into("<II", header, 80, len(header) + len(tables), len(resources))
        tables.extend(resource_table)
    struct.pack_into("<I", header, 88, len(header) + len(tables))
    tables.extend(bytes([len(name)]) + name.encode() + b"\x00\x00\x00")
    struct.pack_into("<I", header, 92, len(header) + len(tables))
    if exports:
        tables.extend(bytes([len(exports), 3]) + struct.pack("<H", 1))
        for offset in exports:
            tables.extend(b"\x01" + struct.pack("<I", offset))
    tables.extend(b"\x00\x00")
    struct.pack_into("<I", header, 104, len(header) + len(tables))
    records = b"".join(fixups)
    tables.extend(struct.pack("<" + "I" * (len(pages) + 1), 0, *([len(records)] * len(pages))))
    struct.pack_into("<I", header, 108, len(header) + len(tables))
    tables.extend(records)
    struct.pack_into("<II", header, 112, len(header) + len(tables), len(modules))
    for module in modules:
        tables.extend(bytes([len(module)]) + module.encode())
    struct.pack_into("<I", header, 120, len(header) + len(tables))
    tables.extend(b"\x00")
    mz = bytearray(64)
    mz[:2] = b"MZ"
    struct.pack_into("<I", mz, 60, 64)
    data_offset = (64 + len(header) + len(tables) + 511) & ~511
    struct.pack_into("<I", header, 128, data_offset)
    image = mz + header + tables
    image.extend(bytes(data_offset - len(image)))
    for i, page in enumerate(pages):
        image.extend(page)
        if i + 1 < len(pages):
            image.extend(bytes(4096 - len(page)))
    return image


class FlatGuest:
    def __init__(self):
        self.code = bytearray(b"\xeb\x03\x31\xc0\xc3")
        self.data = bytearray()
        self.fixups = []

    def field(self, data):
        address = 0x20000 + len(self.data)
        self.data.extend(data)
        return address

    def call(self, module, ordinal, *arguments):
        for value in reversed(arguments):
            if isinstance(value, tuple):
                self.code.extend(b"\xff\x35" + struct.pack("<I", value[0]))
            else:
                self.code.extend(b"\x68" + struct.pack("<I", value))
        self.code.extend(b"\xe8\x00\x00\x00\x00")
        self.fixups.append(struct.pack("<BBhBH", 8, 1, len(self.code) - 4, module, ordinal))
        if arguments:
            self.code.extend(b"\x83\xc4" + bytes([4 * len(arguments)]))

    def exit(self, status):
        self.call(1, 234, 1, status)

    def require(self, comparison, status):
        # Every failure has a distinct guest exit code.
        self.code.extend(comparison + b"\x74\x12")
        self.exit(status)

    def result(self, expected, status):
        self.require(b"\x3d" + struct.pack("<I", expected), status)

    def word(self, address, expected, status):
        self.require(b"\x81\x3d" + struct.pack("<II", address, expected), status)

    def load(self, address):
        self.code.extend(b"\xa1" + struct.pack("<I", address))

    def call_handle(self, module, ordinal, address, *tail):
        self.call(module, ordinal, (address,), *tail)
