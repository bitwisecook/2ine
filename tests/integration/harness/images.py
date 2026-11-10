# Public domain. No rights reserved. Wholly AI-authored.
"""Small LX images and caller-cleaned x86 guests for executable API contracts."""

import struct

# LX header field offsets (OS/2 Toolkit exe386.h names) and layout.
E32_HEADER_SIZE = 196
E32_CPU = 8
E32_MFLAGS = 16
E32_MPAGES = 20
E32_STARTOBJ = 24
E32_STACKOBJ = 32
E32_ESP = 36
E32_PAGESIZE = 40
E32_OBJTAB = 64
E32_OBJCNT = 68
E32_OBJMAP = 72
E32_RSRCTAB = 80
E32_RESTAB = 88
E32_ENTTAB = 92
E32_FPAGETAB = 104
E32_FRECTAB = 108
E32_IMPMOD = 112
E32_IMPPROC = 120
E32_DATAPAGE = 128
E32_AUTODATA = 148
E32CPU386 = 2
NE_OS2 = 1
E32MODDLL = 0x8000
O32_SIZE = 24
OBJPAGELEN = 4096
OBJREAD = 0x0001
OBJWRITE = 0x0002
OBJEXEC = 0x0004
OBJRSRC = 0x0008
OBJBIGDEF = 0x2000
ENTRY32 = 3
# Objects 1 and 2 (code, then data) and their preferred load addresses.
CODE_OBJECT, DATA_OBJECT = 1, 2
CODE_BASE, DATA_BASE = 0x10000, 0x20000
DATA_OBJECT_SIZE = 65536
INITIAL_ESP = 0xFFF0
# DOS stub: a bare 64-byte MZ header whose e_lfanew points just past it.
MZ_HEADER_SIZE = 64
MZ_LFANEW = 60
# Page data starts on a 512-byte file boundary.
PAGE_DATA_ALIGNMENT = 512


def linear_image(
    code,
    fixups,
    modules,
    name,
    library=False,
    data=b"",
    resources=(),
    exports=(),
    entry_table=None,
    export_names=(),
    import_names=b"\0",
):
    header = bytearray(E32_HEADER_SIZE)
    header[:2] = b"LX"
    struct.pack_into("<HH", header, E32_CPU, E32CPU386, NE_OS2)
    objects = [(OBJPAGELEN, CODE_BASE, OBJBIGDEF | OBJEXEC | OBJREAD, 1, 1, 0)]
    objects.append(
        (DATA_OBJECT_SIZE, DATA_BASE, OBJBIGDEF | OBJWRITE | OBJREAD, 2, int(bool(data)), 0)
    )
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
        objects.append((len(resource_bytes), 0, OBJRSRC | OBJREAD, len(pages) + 1, 1, 0))
        pages.append(resource_bytes)
    assert all(len(page) <= OBJPAGELEN for page in pages)
    for offset, value in {
        E32_MFLAGS: E32MODDLL if library else 0,
        E32_MPAGES: len(pages),
        E32_STARTOBJ: CODE_OBJECT,
        E32_STACKOBJ: DATA_OBJECT,
        E32_ESP: INITIAL_ESP,
        E32_PAGESIZE: OBJPAGELEN,
        E32_OBJTAB: E32_HEADER_SIZE,
        E32_OBJCNT: len(objects),
        E32_OBJMAP: E32_HEADER_SIZE + len(objects) * O32_SIZE,
        E32_AUTODATA: DATA_OBJECT,
    }.items():
        struct.pack_into("<I", header, offset, value)
    tables = bytearray()
    for obj in objects:
        tables.extend(struct.pack("<IIIIII", *obj))
    for i, page in enumerate(pages):
        tables.extend(struct.pack("<IHH", i * OBJPAGELEN, len(page), 0))
    if resources:
        struct.pack_into("<II", header, E32_RSRCTAB, len(header) + len(tables), len(resources))
        tables.extend(resource_table)
    struct.pack_into("<I", header, E32_RESTAB, len(header) + len(tables))
    tables.extend(bytes([len(name)]) + name.encode() + b"\x00\x00")
    for ordinal, exported_name in export_names:
        tables.extend(
            bytes([len(exported_name)]) + exported_name.encode() + struct.pack("<H", ordinal)
        )
    tables.extend(b"\x00")
    struct.pack_into("<I", header, E32_ENTTAB, len(header) + len(tables))
    if entry_table is not None:
        tables.extend(entry_table)
    elif exports:
        tables.extend(bytes([len(exports), ENTRY32]) + struct.pack("<H", CODE_OBJECT))
        for offset in exports:
            tables.extend(b"\x01" + struct.pack("<I", offset))
    tables.extend(b"\x00\x00")
    struct.pack_into("<I", header, E32_FPAGETAB, len(header) + len(tables))
    records = b"".join(fixups)
    tables.extend(struct.pack("<" + "I" * (len(pages) + 1), 0, *([len(records)] * len(pages))))
    struct.pack_into("<I", header, E32_FRECTAB, len(header) + len(tables))
    tables.extend(records)
    struct.pack_into("<II", header, E32_IMPMOD, len(header) + len(tables), len(modules))
    for module in modules:
        tables.extend(bytes([len(module)]) + module.encode())
    struct.pack_into("<I", header, E32_IMPPROC, len(header) + len(tables))
    tables.extend(import_names)
    mz = bytearray(MZ_HEADER_SIZE)
    mz[:2] = b"MZ"
    struct.pack_into("<I", mz, MZ_LFANEW, MZ_HEADER_SIZE)
    data_offset = (MZ_HEADER_SIZE + len(header) + len(tables) + PAGE_DATA_ALIGNMENT - 1) & ~(
        PAGE_DATA_ALIGNMENT - 1
    )
    struct.pack_into("<I", header, E32_DATAPAGE, data_offset)
    image = mz + header + tables
    image.extend(bytes(data_offset - len(image)))
    for i, page in enumerate(pages):
        image.extend(page)
        if i + 1 < len(pages):
            image.extend(bytes(OBJPAGELEN - len(page)))
    return image


class FlatGuest:
    def __init__(self):
        self.code = bytearray(b"\xeb\x03\x31\xc0\xc3")
        self.data = bytearray()
        self.fixups = []

    def field(self, data):
        address = DATA_BASE + len(self.data)
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
