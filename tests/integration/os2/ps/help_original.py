#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Qualify original IBM help instance/book/window stages using separately supplied media."""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from tests.integration.harness.images import FlatGuest, linear_image


def numbered_panel(book):
    data = book.read_bytes()
    if len(data) < 155 or data[:3] != b"HSP":
        raise ValueError("Expected an original HSP help book")
    count, offset = struct.unpack_from("<HI", data, 22)
    toc_offsets = struct.unpack_from("<I", data, 18)[0]
    if not count or offset + 4 * count > len(data):
        raise ValueError("Help book has no valid numbered panel map")
    numbers = struct.unpack_from("<" + "H" * count, data, offset)
    entries = struct.unpack_from("<" + "H" * count, data, offset + 2 * count)
    for number, entry in zip(numbers, entries, strict=True):
        location = toc_offsets + 4 * entry
        if location + 4 > len(data):
            raise ValueError("Invalid help table of contents")
        record = struct.unpack_from("<I", data, location)[0]
        if record + 3 > len(data):
            raise ValueError("Invalid help table entry")
        if not data[record + 1] & 0x40 and data[record + 2]:
            return number
    raise ValueError("Help book has no visible numbered text panel")


def nonzero(guest, status):
    guest.code.extend(b"\x85\xc0\x75\x12")
    guest.exit(status)


def store(guest, address):
    guest.code.extend(b"\xa3" + struct.pack("<I", address))


def pump(guest, anchor, count):
    message = guest.field(bytes(32))
    counter = guest.field(struct.pack("<I", count))
    start = len(guest.code)
    guest.call(2, 918, (anchor,), message, 0, 0, 0, 1)
    guest.code.extend(b"\x85\xc0\x0f\x84")
    skip = len(guest.code)
    guest.code.extend(bytes(4))
    guest.call(2, 912, (anchor,), message)
    struct.pack_into("<i", guest.code, skip, len(guest.code) - skip - 4)
    guest.call(1, 229, 1)
    guest.code.extend(b"\xff\x0d" + struct.pack("<I", counter) + b"\x0f\x85")
    back = len(guest.code)
    guest.code.extend(struct.pack("<i", start - back - 4))


def image(panel, hold_seconds=0):
    guest = FlatGuest()
    anchor, queue, handle, queried = (guest.field(bytes(4)) for _ in range(4))
    library = guest.field(b"C:\\OS2\\HELP\\HMHELP.HLP\0")
    title = guest.field(b"Original OS/2 Help Regression\0")
    initialization = guest.field(struct.pack("<11I", 44, 0, 0, 0, 0, 0, 0, 0, title, 0, library))
    guest.call(2, 763, 0)
    nonzero(guest, 10)
    store(guest, anchor)
    guest.call(2, 716, (anchor,), 0)
    nonzero(guest, 11)
    store(guest, queue)
    guest.call(3, 51, (anchor,), initialization)
    nonzero(guest, 12)
    store(guest, handle)
    guest.word(initialization + 4, 0, 13)
    guest.call(2, 920, (handle,), 0x22B, 0, 0)
    pump(guest, anchor, 300)
    guest.call(2, 920, (handle,), 0x23C, 3 << 16, 0)
    nonzero(guest, 14)
    store(guest, queried)
    guest.call(2, 772, (anchor,), (queried,))
    guest.result(1, 15)
    guest.call(2, 920, (handle,), 0x222, panel, 0)
    pump(guest, anchor, 300)
    guest.call(2, 920, (handle,), 0x23C, 1 << 16, 0)
    nonzero(guest, 16)
    store(guest, queried)
    for control in (0x8008, 105, 0x8008):
        guest.call(2, 899, (queried,), control)
        nonzero(guest, 20)
        store(guest, queried)
    written = guest.field(bytes(4))
    guest.call(1, 282, 1, queried, 4, written)
    guest.result(0, 21)
    if hold_seconds:
        pump(guest, anchor, hold_seconds * 1000)
    guest.call(3, 52, (handle,))
    guest.result(1, 17)
    guest.call(2, 726, (queue,))
    guest.result(1, 18)
    guest.call(2, 888, (anchor,))
    guest.result(1, 19)
    guest.exit(0)
    return linear_image(
        guest.code, guest.fixups, ["DOSCALLS", "PMWIN", "HELPMGR"], "HELPTEST", data=guest.data
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--helpmgr", type=Path, required=True)
    parser.add_argument("--resources", type=Path, required=True, help="Original HPMGRMRI.DLL")
    parser.add_argument("--book", type=Path, required=True, help="Original HMHELP.HLP system book")
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--backend", choices=("unicorn", "native"), required=True)
    parser.add_argument(
        "--require-text",
        action="store_true",
        help="Fail unless original panel text reaches GpiCharStringAt",
    )
    parser.add_argument(
        "--visible", action="store_true", help="Use the host display for manual rendering review"
    )
    parser.add_argument(
        "--hold-seconds",
        type=int,
        default=0,
        help="Pump the visible help window before clean destruction (0–60)",
    )
    args = parser.parse_args()
    if args.hold_seconds < 0 or args.hold_seconds > 60:
        parser.error("--hold-seconds must be between 0 and 60")
    panel = numbered_panel(args.book)
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    if any(work.iterdir()):
        parser.error("--work must be empty")
    (work / "OS2/DLL").mkdir(parents=True)
    (work / "OS2/HELP").mkdir()
    inputs = {
        "OS2/DLL/HELPMGR.DLL": args.helpmgr,
        "OS2/DLL/HPMGRMRI.DLL": args.resources,
        "OS2/HELP/HMHELP.HLP": args.book,
    }
    for relative, source in inputs.items():
        shutil.copyfile(source, work / relative)
    (work / "inputs.json").write_text(
        json.dumps(
            {
                name: hashlib.sha256(source.read_bytes()).hexdigest()
                for name, source in inputs.items()
            },
            indent=2,
        )
        + "\n"
    )
    config = work / "2ine.cfg"
    config.write_text(f"mountpoint.C = {work}\n")
    executable = work / "HELPTEST.EXE"
    executable.write_bytes(image(panel, args.hold_seconds))
    environment = dict(
        os.environ,
        TWOINE_CONFIG=str(config),
        TWOINE_BACKEND=args.backend,
        TWOINE_LIBPATH=str(args.build.resolve()),
        TWOINE_CURRENT_DRIVE="C",
        TWOINE_BALL=str(work),
        TWOINE_IPC_DIR=str(work / "ipc"),
        LIBPATH="C:\\OS2\\DLL;C:\\OS2",
        HELP="C:\\OS2\\HELP",
        DPATH="C:\\OS2\\HELP;C:\\OS2",
        TRACE_NATIVE="1",
        SDL_VIDEO_DRIVER="dummy",
        SDL_RENDER_DRIVER="software",
        SDL_AUDIO_DRIVER="dummy",
    )
    if args.visible:
        environment.pop("SDL_VIDEO_DRIVER", None)
    (work / "ipc").mkdir()
    with (work / "stdout.log").open("wb") as stdout, (work / "stderr.log").open("wb") as stderr:
        result = subprocess.run(
            [str(args.build.resolve() / "lx_loader"), str(executable)],
            cwd=work,
            env=environment,
            stdout=stdout,
            stderr=stderr,
            timeout=20 + args.hold_seconds * 3,
        )
    trace = (work / "stderr.log").read_text(errors="replace")
    if result.returncode:
        raise RuntimeError(
            f"Original help regression failed with status {result.returncode}; see {work / 'stderr.log'}"
        )
    output = (work / "stdout.log").read_bytes()
    if len(output) != 4:
        raise RuntimeError("Original help probe did not report its page window")
    page_window = struct.unpack("<I", output)[0]
    paint_windows = []
    page_text = False
    other_text = False
    for line in trace.splitlines():
        painting = re.search(r"WinBeginPaint\((\d+),", line)
        if painting:
            paint_windows.append(int(painting.group(1)))
        elif "WinEndPaint(" in line and paint_windows:
            paint_windows.pop()
        elif "GpiCharStringAt(" in line:
            if paint_windows and paint_windows[-1] == page_window:
                page_text = True
            else:
                other_text = True
    (work / "result.json").write_text(
        json.dumps(
            {
                "status": result.returncode,
                "panel": panel,
                "pageWindow": page_window,
                "panelTextDrawingObserved": page_text,
                "otherTextDrawingObserved": other_text,
            },
            indent=2,
        )
        + "\n"
    )
    if args.require_text and not page_text:
        raise RuntimeError("Original help windows exist, but no panel text drawing was observed")
    print(
        "Original IBM help instance, book reads, contents/panel windows and clean destruction passed"
    )
    print(
        "Panel text drawing observed" if page_text else "Panel text rendering remains unqualified"
    )


if __name__ == "__main__":
    main()
