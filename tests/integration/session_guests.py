# Public domain. No rights reserved. Wholly AI-authored.
"""Generate two independent LX apps that exchange named shared-memory data."""

import argparse
import struct
from pathlib import Path

from images import FlatGuest, linear_image

ADDRESS = 0x30000000


def announce(guest, text):
    message = guest.field(text.encode("ascii"))
    count = guest.field(bytes(4))
    guest.call(1, 282, 1, message, len(text), count)
    guest.result(0, 70)


def writer():
    guest = FlatGuest()
    output = guest.field(bytes(4))
    name = guest.field(b"\\SHAREMEM\\SESSION-FIXTURE\0")
    remaining = guest.field(struct.pack("<I", 500))
    guest.call(1, 300, output, name, 4096, 19)
    guest.result(0, 71)
    guest.word(output, ADDRESS, 72)
    guest.code.extend(b"\xc7\x05" + struct.pack("<II", ADDRESS, 0x11223344))
    announce(guest, "writer-ready\n")
    loop = len(guest.code)
    guest.call(1, 229, 10)
    guest.code.extend(b"\x81\x3d" + struct.pack("<II", ADDRESS + 4, 0x55667788))
    guest.code.extend(b"\x0f\x84" + bytes(4))
    matched = len(guest.code) - 4
    guest.code.extend(b"\xff\x0d" + struct.pack("<I", remaining) + b"\x0f\x85")
    guest.code.extend(struct.pack("<i", loop - len(guest.code) - 4))
    guest.exit(73)
    struct.pack_into("<i", guest.code, matched, len(guest.code) - matched - 4)
    announce(guest, "writer-shared-ok\n")
    guest.call(1, 229, 30000)
    guest.exit(0)
    return linear_image(guest.code, guest.fixups, ["DOSCALLS"], "SESSIONWRITE", data=guest.data)


def reader():
    guest = FlatGuest()
    output = guest.field(bytes(4))
    name = guest.field(b"\\SHAREMEM\\session-fixture\0")
    guest.call(1, 301, output, name, 3)
    guest.result(0, 74)
    guest.word(output, ADDRESS, 75)
    guest.word(ADDRESS, 0x11223344, 76)
    guest.code.extend(b"\xc7\x05" + struct.pack("<II", ADDRESS + 4, 0x55667788))
    announce(guest, "reader-shared-ok\n")
    guest.call(1, 229, 1500)
    guest.word(ADDRESS, 0x11223344, 77)
    announce(guest, "reader-after-peer-exit\n")
    guest.call(1, 229, 30000)
    guest.exit(0)
    return linear_image(guest.code, guest.fixups, ["DOSCALLS"], "SESSIONREAD", data=guest.data)


def desktop_child(parent):
    guest = FlatGuest()
    if parent:
        filename = guest.field(b"C:\\OS2\\CHILD.EXE\0")
        command = guest.field(b"C:\\OS2\\CHILD.EXE\0\0")
        result = guest.field(bytes(8))
        guest.call(1, 283, 0, 0, 0, command, 0, result, filename)
        guest.result(0, 80)
        guest.word(result + 4, 0, 81)
        announce(guest, "desktop-parent-complete\n")
    else:
        announce(guest, "desktop-child-ready\n")
        guest.call(1, 229, 2000)
    guest.exit(0)
    return linear_image(
        guest.code,
        guest.fixups,
        ["DOSCALLS"],
        "DESKTOPPARENT" if parent else "DESKTOPCHILD",
        data=guest.data,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    (args.directory / "ONE.EXE").write_bytes(writer())
    (args.directory / "TWO.EXE").write_bytes(reader())
    (args.directory / "PARENT.EXE").write_bytes(desktop_child(True))
    (args.directory / "CHILD.EXE").write_bytes(desktop_child(False))


if __name__ == "__main__":
    main()
