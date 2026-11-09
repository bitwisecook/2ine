# Public domain. No rights reserved. Wholly AI-authored.
"""Small filesystem fixtures with nested files and fragmented FAT chains."""

import struct

PAYLOAD = b"MZ" + bytes(range(256)) * 6


def fat_image(bits=12):
    sectors, fat_sectors, root_entries = (2880, 9, 224) if bits == 12 else (8500, 34, 512)
    data = bytearray(sectors * 512)
    data[:11] = b"\xeb\x3c\x90TWOINET "
    struct.pack_into(
        "<HBHBHHBHHHII",
        data,
        11,
        512,
        1,
        1,
        2,
        root_entries,
        sectors,
        0xF0,
        fat_sectors,
        18,
        2,
        0,
        0,
    )
    data[38] = 0x29
    struct.pack_into("<I", data, 39, 0x12345678)
    data[43:54] = b"TEST_MEDIA "
    data[54:62] = b"FAT12   " if bits == 12 else b"FAT16   "
    data[510:512] = b"\x55\xaa"
    fat = bytearray(fat_sectors * 512)

    def set_cluster(cluster, value):
        if bits == 16:
            struct.pack_into("<H", fat, cluster * 2, value)
        else:
            at = cluster + cluster // 2
            old = struct.unpack_from("<H", fat, at)[0]
            value = (old & 15) | (value << 4) if cluster & 1 else (old & 0xF000) | value
            struct.pack_into("<H", fat, at, value)

    end = 0xFFF if bits == 12 else 0xFFFF
    set_cluster(0, end & ~15)
    set_cluster(1, end)
    for cluster, value in [(2, end), (4, end), (3, 5), (5, 7), (7, 9), (9, end)]:
        set_cluster(cluster, value)
    for copy in range(2):
        start = (1 + copy * fat_sectors) * 512
        data[start : start + len(fat)] = fat
    root = (1 + 2 * fat_sectors) * 512
    content = root + ((root_entries * 32 + 511) // 512) * 512

    def entry(at, name, attributes, cluster, size):
        data[at : at + 11] = name
        data[at + 11] = attributes
        struct.pack_into("<H", data, at + 26, cluster)
        struct.pack_into("<I", data, at + 28, size)

    entry(root, b"TEST_MEDIA ", 8, 0, 0)
    entry(root + 32, b"INSTALL    ", 16, 2, 0)
    entry(root + 64, b"README  TXT", 0, 4, 5)
    entry(content, b"SETUP   EXE", 0, 3, len(PAYLOAD))
    data[content + 2 * 512 : content + 2 * 512 + 5] = b"media"
    for i, cluster in enumerate([3, 5, 7, 9]):
        part = PAYLOAD[i * 512 : (i + 1) * 512]
        at = content + (cluster - 2) * 512
        data[at : at + len(part)] = part
    return bytes(data)


def iso_image(joliet=False, payload=PAYLOAD, filename="SETUP.EXE"):
    assert len(payload) <= 6 * 2048
    data = bytearray(28 * 2048)

    def both(at, value, width=4):
        fmt = "I" if width == 4 else "H"
        struct.pack_into("<" + fmt, data, at, value)
        struct.pack_into(">" + fmt, data, at + width, value)

    def record(at, extent, size, name, directory=False):
        raw = name if isinstance(name, bytes) else name.encode("utf-16-be" if joliet else "ascii")
        length = 33 + len(raw) + (not len(raw) % 2)
        data[at] = length
        both(at + 2, extent)
        both(at + 10, size)
        data[at + 25] = 2 if directory else 0
        both(at + 28, 1, 2)
        data[at + 32] = len(raw)
        data[at + 33 : at + 33 + len(raw)] = raw
        return length

    for sector, kind in [(16, 1), *(([(17, 2)]) if joliet else [])]:
        base = sector * 2048
        data[base : base + 7] = bytes([kind]) + b"CD001\x01"
        data[base + 40 : base + 72] = b"TEST_MEDIA".ljust(32, b" ")
        both(base + 80, 28)
        both(base + 128, 2048, 2)
        if kind == 2:
            data[base + 88 : base + 91] = b"%/E"
        record(base + 156, 20, 2048, b"\x00", True)
    end = (18 if joliet else 17) * 2048
    data[end : end + 7] = b"\xffCD001\x01"
    at = 20 * 2048
    at += record(at, 20, 2048, b"\x00", True)
    at += record(at, 20, 2048, b"\x01", True)
    record(at, 21, 2048, "INSTALL", True)
    at = 21 * 2048
    at += record(at, 21, 2048, b"\x00", True)
    at += record(at, 20, 2048, b"\x01", True)
    record(at, 22, len(payload), filename + ";1")
    data[22 * 2048 : 22 * 2048 + len(payload)] = payload
    return bytes(data)
