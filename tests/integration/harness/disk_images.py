# Public domain. No rights reserved. Wholly AI-authored.
"""Small filesystem fixtures with nested files and fragmented FAT chains."""

import struct

PAYLOAD = b"MZ" + bytes(range(256)) * 6

# FAT boot sector (BIOS parameter block) layout and the fixture geometry:
# (total sectors, sectors per FAT, root directory entries).
SECTOR_SIZE = 512
BPB_OFFSET = 11
EBPB_SIGNATURE_OFFSET = 38
EXTENDED_BOOT_SIGNATURE = 0x29
VOLUME_SERIAL = 0x12345678
BOOT_SIGNATURE_OFFSET = 510
BOOT_SIGNATURE = b"\x55\xaa"
MEDIA_DESCRIPTOR = 0xF0
SECTORS_PER_TRACK = 18
FLOPPY_1440K_GEOMETRY = (2880, 9, 224)
FAT16_GEOMETRY = (8500, 34, 512)
DIRECTORY_ENTRY_SIZE = 32
ATTR_VOLUME_ID = 0x08
ATTR_DIRECTORY = 0x10
FIRST_DATA_CLUSTER = 2


def fat_image(bits=12):
    sectors, fat_sectors, root_entries = FLOPPY_1440K_GEOMETRY if bits == 12 else FAT16_GEOMETRY
    data = bytearray(sectors * SECTOR_SIZE)
    data[:11] = b"\xeb\x3c\x90TWOINET "
    struct.pack_into(
        "<HBHBHHBHHHII",
        data,
        BPB_OFFSET,
        SECTOR_SIZE,
        1,
        1,
        2,
        root_entries,
        sectors,
        MEDIA_DESCRIPTOR,
        fat_sectors,
        SECTORS_PER_TRACK,
        2,
        0,
        0,
    )
    data[EBPB_SIGNATURE_OFFSET] = EXTENDED_BOOT_SIGNATURE
    struct.pack_into("<I", data, EBPB_SIGNATURE_OFFSET + 1, VOLUME_SERIAL)
    data[43:54] = b"TEST_MEDIA "
    data[54:62] = b"FAT12   " if bits == 12 else b"FAT16   "
    data[BOOT_SIGNATURE_OFFSET : BOOT_SIGNATURE_OFFSET + 2] = BOOT_SIGNATURE
    fat = bytearray(fat_sectors * SECTOR_SIZE)

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
        start = (1 + copy * fat_sectors) * SECTOR_SIZE
        data[start : start + len(fat)] = fat
    root = (1 + 2 * fat_sectors) * SECTOR_SIZE
    content = (
        root
        + ((root_entries * DIRECTORY_ENTRY_SIZE + SECTOR_SIZE - 1) // SECTOR_SIZE) * SECTOR_SIZE
    )

    def entry(at, name, attributes, cluster, size):
        data[at : at + 11] = name
        data[at + 11] = attributes
        struct.pack_into("<H", data, at + 26, cluster)
        struct.pack_into("<I", data, at + 28, size)

    entry(root, b"TEST_MEDIA ", ATTR_VOLUME_ID, 0, 0)
    entry(root + DIRECTORY_ENTRY_SIZE, b"INSTALL    ", ATTR_DIRECTORY, 2, 0)
    entry(root + 2 * DIRECTORY_ENTRY_SIZE, b"README  TXT", 0, 4, 5)
    entry(content, b"SETUP   EXE", 0, 3, len(PAYLOAD))
    data[content + 2 * SECTOR_SIZE : content + 2 * SECTOR_SIZE + 5] = b"media"
    for i, cluster in enumerate([3, 5, 7, 9]):
        part = PAYLOAD[i * SECTOR_SIZE : (i + 1) * SECTOR_SIZE]
        at = content + (cluster - FIRST_DATA_CLUSTER) * SECTOR_SIZE
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
