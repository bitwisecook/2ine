#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Validate native/guest printer jobs, PDF image streams and PCL raster pages."""

import argparse
import json
import os
import re
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

from images import FlatGuest, linear_image


def pdf_pages(path, expected):
    data = path.read_bytes()
    assert data.startswith(b"%PDF-1.4\n") and data.endswith(b"%%EOF\n")
    xref = re.search(rb"startxref\n(\d+)\n", data)
    assert xref is not None, "PDF is missing its cross-reference offset"
    start = int(xref[1])
    assert data[start:].startswith(b"xref\n")
    assert b"/Count " + str(expected).encode() + b" /Kids" in data
    images = []
    for match in re.finditer(
        rb"/Subtype /Image /Width (\d+) /Height (\d+).*?/Length (\d+) >>\nstream\n", data
    ):
        width, height, length = map(int, match.groups())
        pixels = zlib.decompress(data[match.end() : match.end() + length])
        assert len(pixels) == width * height * 3
        assert (width, height) == (2480, 3508)
        images.append((width, height, pixels))
    assert len(images) == expected
    return images


def pcl_pages(path, expected=2, ink_row=3508 - 151):
    data = path.read_bytes()
    assert data.startswith(b"\x1b%-12345X@PJL ENTER LANGUAGE=PCL\r\n\x1bE")
    assert data.endswith(b"\x1bE\x1b%-12345X")
    at = 0
    pages = []
    while True:
        at = data.find(b"\x1b*r1A", at)
        if at < 0:
            break
        at += len(b"\x1b*r1A")
        rows = []
        while data[at : at + 4] != b"\x1b*rB":
            header = re.match(rb"\x1b\*b(\d+)W", data[at:])
            assert header
            length = int(header[1])
            assert length == 310
            at += header.end()
            rows.append(data[at : at + length])
            at += length
        assert len(rows) == 3508
        pages.append(rows)
        at += 4
    assert len(pages) == expected
    for rows in pages:
        assert rows[0] == bytes(310)
        assert any(rows[ink_row][100 // 8 : 400 // 8])
    return pages


def guest_program():
    g = FlatGuest()
    counts = g.field(bytes(12))
    buffer = g.field(b"HEAD" + bytes(512) + b"TAIL") + 4
    for level, record in ((0, 13), (1, 44), (3, 44), (4, 44), (5, 4), (6, 52)):
        g.call(4, 399, 0, level, buffer, 512, counts, counts + 4, counts + 8, 0)
        g.result(0, 10)
        g.word(counts, 2, 11)
        g.word(counts + 4, 2, 12)
        for offset in (0, record):
            if level in (0, 1):
                g.word(buffer + offset, int.from_bytes(b"2INE", "little"), 13)
            else:
                g.load(buffer + offset)
                g.require(b"\x81\x38" + b"2INE", 14)
            if level in (3, 4, 6):
                g.word(buffer + offset + 10, 4 if offset == 0 else 0, 120)
                g.load(buffer + offset + 40)
                g.require(b"\x81\x38" + struct.pack("<I", 40), 110)
                g.require(b"\x81\x78\x04" + struct.pack("<I", 1), 111)
                g.require(b"\x81\x78\x08" + b"2INE", 112)
        g.word(buffer - 4, int.from_bytes(b"HEAD", "little"), 15)
        g.word(buffer + 512, int.from_bytes(b"TAIL", "little"), 16)
    app, key = g.field(b"PM_SPOOLER\0"), g.field(b"QUEUE\0")
    profile = g.field(bytes(32))
    g.call(5, 115, 0xFFFFFFFF, app, key, 0, profile, 32)
    g.result(8, 121)
    g.word(profile, int.from_bytes(b"2INE", "little"), 122)
    g.word(profile + 4, int.from_bytes(b"PDF;", "little"), 123)
    g.call(5, 116, 0xFFFFFFFF, app, key, g.field(b"2INEPCL;\0"))
    g.result(1, 124)
    g.call(4, 399, 0, 3, buffer, 512, counts, counts + 4, counts + 8, 0)
    g.result(0, 125)
    g.word(buffer + 10, 0, 126)
    g.word(buffer + 44 + 10, 4, 127)
    g.call(5, 116, 0xFFFFFFFF, app, key, 0)
    g.result(1, 128)
    needed = counts + 8
    g.call(4, 397, 0, g.field(b"2INEPDF\0"), 3, buffer, 512, needed)
    g.result(0, 17)
    g.load(buffer + 36)
    g.require(b"\x81\x38" + b"2INE", 18)
    g.load(buffer + 40)
    g.require(b"\x81\x38" + struct.pack("<I", 40), 113)
    g.require(b"\x81\x78\x08" + b"2INE", 114)
    g.call(4, 399, 0, 3, 0, 0, counts, counts + 4, needed, 0)
    g.result(234, 19)
    g.word(counts, 0, 20)
    g.word(counts + 4, 2, 21)
    g.call(2, 763, 0)
    g.result(1, 22)
    driver = g.field(b"2INEPDF\0")
    fields = g.field(struct.pack("<IIII", driver, driver, 0, 0))
    dc = g.field(bytes(4))
    g.call(3, 610, 1, 2, g.field(b"*\0"), 4, fields, 0)
    g.code.extend(b"\xa3" + struct.pack("<I", dc))
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 23)
    form = g.field(bytes(68) + b"TAIL")
    g.call(3, 608, (dc,), 0, 1, form)
    g.result(1, 24)
    g.word(form + 68, int.from_bytes(b"TAIL", "little"), 25)
    g.call(3, 605, (dc,), 0, 4, g.field(struct.pack("<I", 8150)), 0, 0)
    g.result(1, 26)
    g.call(3, 605, (dc,), 8150, 12, g.field(b"Guest Print\0"), 0, 0)
    g.result(1, 27)
    g.call(3, 605, (dc,), 8151, 0, 0, 0, 0)
    g.result(1, 28)
    g.call(3, 604, (dc,))
    g.result(0, 29)
    g.call(2, 888, 1)
    g.result(1, 30)
    g.exit(0)
    return linear_image(
        g.code,
        g.fixups,
        ["DOSCALLS", "PMWIN", "PMGPI", "PMSPL", "PMSHAPI"],
        "PRINTING",
        data=g.data,
    )


def open32_program(queue="2INEPDF"):
    g = FlatGuest()
    record = bytearray(66)
    struct.pack_into("<I", record, 0, 66)
    struct.pack_into("<I", record, 20, 0x500)  # Return default printer and a drawing DC.
    bad = bytearray(record)
    struct.pack_into("<I", bad, 0, 65)
    g.call(2, 530, g.field(bad))
    g.result(0, 59)
    g.call(2, 48)
    g.result(1, 60)
    dialog = g.field(b"HEAD" + record + b"TAIL") + 4
    dc = g.field(bytes(4))
    g.call(2, 530, dialog)
    g.result(1, 40)
    g.word(dialog - 4, int.from_bytes(b"HEAD", "little"), 41)
    g.word(dialog + 66, int.from_bytes(b"TAIL", "little"), 42)
    g.load(dialog + 16)
    g.code.extend(b"\xa3" + struct.pack("<I", dc))
    for index, value in ((2, 2), (8, 2480), (10, 3508), (88, 300), (90, 300)):
        g.call(2, 269, (dc,), index)
        g.result(value, 43)
    g.call(2, 530, dialog)
    g.result(0, 61)  # RETURNDEFAULT cannot replace existing caller-owned handles.
    g.call(2, 48)
    g.result(0x1003, 62)
    mode = g.field(bytes(4))
    name = g.field(queue.encode() + b"\0")
    g.call(2, 404, (dialog + 8,))
    g.require(b"\x66\x81\x78\x24\x80\x00", 44)  # DEVMODE.dmSize=128 from OS2WIN.H.
    g.code.extend(b"\xa3" + struct.pack("<I", mode))
    g.require(b"\x81\x78\x04" + (queue[4:].encode() + b"\0"), 67)
    g.call(2, 404, (dialog + 12,))
    g.require(b"\x66\x81\x78\x06\x01\x00", 68)  # DN_DEFAULTPRN for either queue.
    g.code.extend(b"\x89\xc2\x0f\xb7\x40\x02\x01\xd0")
    g.require(b"\x81\x38" + b"2INE", 69)
    g.require(b"\x81\x78\x04" + (queue[4:].encode() + b"\0"), 70)
    g.call(2, 408, (dialog + 12,))
    information = g.field(bytes(4))
    g.call(2, 78, name, name, 0, (mode,))
    g.code.extend(b"\xa3" + struct.pack("<I", information))
    g.require(b"\x85\xc0\x0f\x95\xc0\x3c\x01", 63)
    g.call(2, 135, (information,))
    g.result(1, 64)
    other = g.field(("2INEPCL" if queue == "2INEPDF" else "2INEPDF").encode() + b"\0")
    g.call(2, 64, other, other, 0, (mode,))
    g.result(0, 71)  # A DEVMODE for a different device is not silently reused.
    g.call(2, 300)
    g.result(1801, 72)
    g.load(mode)
    g.code.extend(b"\xc7\x00AAAA")
    for offset in range(4, 32, 4):
        g.code.extend(b"\xc7\x40" + bytes([offset]) + b"AAAA")
    g.call(2, 78, name, name, 0, (mode,))
    g.result(0, 73)  # dmDeviceName must terminate within its SDK 32-byte field.
    g.call(2, 300)
    g.result(87, 74)
    g.load(mode)
    g.code.extend(b"\xc7\x00" + b"2INE")
    for offset in range(4, 32, 4):
        value = queue[4:].encode() + b"\0" if offset == 4 else bytes(4)
        g.code.extend(b"\xc7\x40" + bytes([offset]) + value)
    g.load(mode)
    g.code.extend(b"\x66\xc7\x40\x2c\x03\x00")  # Neither portrait nor landscape.
    g.call(2, 64, name, name, 0, (mode,))
    g.result(0, 65)
    g.call(2, 300)
    g.result(50, 66)
    g.load(mode)
    g.code.extend(b"\x66\xc7\x40\x2c\x01\x00")
    g.call(2, 408, (dialog + 8,))
    needed, output = g.field(bytes(4)), g.field(bytes(256) + b"TAIL")
    g.call(3, 381, 0, name, 3, output, 256, needed)
    g.result(0, 45)
    g.load(output)
    g.require(b"\x81\x38" + b"2INE", 46)
    g.load(output + 24)
    g.require(b"\x81\x38" + b"2INE", 47)
    g.word(output + 256, int.from_bytes(b"TAIL", "little"), 48)
    info = g.field(struct.pack("<III", 12, g.field(b"Open32 Print\0"), 0))
    saved = g.field(bytes(4))
    g.call(2, 576, (dc,))
    g.code.extend(b"\xa3" + struct.pack("<I", saved))
    g.call(2, 674, (dc,), info)
    g.result(1, 49)
    g.call(2, 675, (dc,))
    g.result(1, 50)
    g.call(2, 572, (dc,), (saved,))
    g.result(1, 58)  # Saved drawing state must not reset the active print job/page.
    brush = g.field(bytes(4))
    g.call(2, 97, 0xFF)  # COLORREF red.
    g.code.extend(b"\xa3" + struct.pack("<I", brush))
    g.call(2, 585, (dc,), (brush,))
    g.call(2, 539, (dc,), 100, 100, 400, 300)
    g.result(1, 51)
    g.call(2, 689, (dc,), 100, 400, g.field(b"Open32 Print"), 12)
    g.result(1, 52)
    g.call(2, 172, (dc,))
    g.result(1, 53)
    g.call(2, 171, (dc,))
    g.result(1, 54)
    g.call(2, 135, (dc,))
    g.result(1, 55)
    g.call(2, 140, (brush,))
    g.result(1, 56)
    for offset in (8, 12):
        g.call(2, 401, (dialog + offset,))
        g.result(0, 57)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX", "PMSPL"], "PRINT32", data=g.data)


def no_default_program():
    g = FlatGuest()
    record = bytearray(66)
    struct.pack_into("<I", record, 0, 66)
    struct.pack_into("<I", record, 20, 0x500)
    dialog = g.field(b"HEAD" + record + b"TAIL") + 4
    g.call(2, 530, dialog)
    g.result(0, 80)
    g.call(2, 48)
    g.result(0x1008, 81)  # PDERR_NODEFAULTPRN; no fallback for explicit empty/unknown.
    original = b"HEAD" + record + b"TAIL"
    for offset in range(0, len(original) - 3, 4):
        g.word(dialog - 4 + offset, int.from_bytes(original[offset : offset + 4], "little"), 82)
    g.word(dialog + 66, int.from_bytes(b"TAIL", "little"), 83)
    g.exit(0)
    return linear_image(g.code, g.fixups, ["DOSCALLS", "PMWINX"], "NODEF", data=g.data)


def queue_profile(path, queue):
    if queue is None:
        path.unlink(missing_ok=True)
        return
    app, key, data = b"PM_SPOOLER\0", b"QUEUE\0", queue.encode() + b"\0"
    path.write_bytes(
        b"2INEPRF1" + struct.pack("<IIII", 1, len(app), len(key), len(data)) + app + key + data
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("unicorn", "native"), required=True)
    parser.add_argument("--work", type=Path)
    args = parser.parse_args()
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix="2ine-print-") as temporary:
        work = args.work.resolve() if args.work else Path(temporary)
        work.mkdir(parents=True, exist_ok=True)
        environment = dict(
            os.environ,
            SDL_VIDEO_DRIVER="dummy",
            SDL_RENDER_DRIVER="software",
            LD_LIBRARY_PATH=str(build),
            TWOINE_USER_PROFILE=str(work / "user.profile"),
            TWOINE_SYSTEM_PROFILE=str(work / "system.profile"),
        )
        subprocess.run(
            [str(build / "tests/twoine_printing_tests"), str(work)],
            env=environment,
            check=True,
            timeout=30,
        )
        command = work / "capture-spooler"
        command.write_text(
            "#!/usr/bin/env python3\n"
            "import json, os, pathlib, sys\n"
            "root = pathlib.Path(os.environ['TWOINE_PRINT_TEST_CAPTURE'])\n"
            "root.joinpath('spool-argv.json').write_text(json.dumps(sys.argv[1:]))\n"
            "root.joinpath('spool.pcl').write_bytes(sys.stdin.buffer.read())\n"
            "sys.exit(int(os.environ.get('TWOINE_PRINT_TEST_STATUS', '0')))\n"
        )
        command.chmod(0o700)
        submission = dict(environment)
        submission.pop("TWOINE_PRINT_OUTPUT", None)
        printer = "Test;$(should_not_run)"
        submission.update(
            TWOINE_PRINT_PRINTER=printer,
            TWOINE_PRINT_TEST_CAPTURE=str(work),
        )
        driver = str(build / "tests/twoine_printing_tests")
        subprocess.run(
            [driver, "--spool", str(command), str(work / "printing.pcl"), "1"],
            env=submission,
            cwd=work,
            check=True,
            timeout=20,
        )
        assert json.loads((work / "spool-argv.json").read_text()) == [
            "-d",
            printer,
            "-n",
            "1",
            "-t",
            'Print "quoted" $(touch ignored)',
            "-o",
            "raw",
            "--",
            "-",
        ]
        assert (work / "spool.pcl").read_bytes().startswith(b"\x1b%-12345X@PJL ENTER LANGUAGE=PCL")
        assert not (work / "ignored").exists()
        submission["TWOINE_PRINT_TEST_STATUS"] = "7"
        subprocess.run(
            [driver, "--spool", str(command), str(work / "printing.pcl"), "0"],
            env=submission,
            cwd=work,
            check=True,
            timeout=20,
        )
        helpers = work / "CUPS helpers"
        helpers.mkdir(exist_ok=True)
        (helpers / "lp").write_bytes(command.read_bytes())
        (helpers / "lp").chmod(0o700)
        (helpers / "lpstat").write_text(
            "#!/bin/sh\nprintf 'Fixture_Printer accepting requests\\n'\n"
        )
        (helpers / "lpstat").chmod(0o700)
        submission.update(TWOINE_CUPS_BINDIR=str(helpers), TWOINE_PRINT_TEST_STATUS="0")
        subprocess.run(
            [driver, "--cups", str(work / "printing.pcl")],
            env=submission,
            cwd=work,
            check=True,
            timeout=20,
        )
        assert json.loads((work / "spool-argv.json").read_text())[:2] == ["-d", "Fixture_Printer"]
        pages = pdf_pages(work / "printing.pdf", 2)
        for (_, height, pixels), color in zip(pages, (b"\xff\0\0", b"\0\0\xff"), strict=True):
            offset = ((height - 151) * 2480 + 150) * 3
            assert pixels[offset : offset + 3] == color
        pcl_pages(work / "printing.pcl")
        guest = work / "printing.exe"
        guest.write_bytes(guest_program())
        environment.update(
            TWOINE_BACKEND=args.backend,
            TWOINE_LIBPATH=str(build),
            TWOINE_PRINT_OUTPUT=str(work / "guest.pdf"),
        )
        result = subprocess.run(
            [str(build / "lx_loader"), str(guest)],
            cwd=work,
            env=environment,
            capture_output=True,
            timeout=20,
        )
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        pdf_pages(work / "guest.pdf", 1)
        guest.write_bytes(open32_program())
        environment["TWOINE_PRINT_OUTPUT"] = str(work / "open32.pdf")
        result = subprocess.run(
            [str(build / "lx_loader"), str(guest)],
            cwd=work,
            env=environment,
            capture_output=True,
            timeout=20,
        )
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        pdf_pages(work / "open32.pdf", 1)
        for label, user, system, queue in (
            ("user-pcl", "2INEPCL;", None, "2INEPCL"),
            ("system-pcl", None, "2INEPCL;", "2INEPCL"),
            ("user-pdf", "2INEPDF;", "2INEPCL;", "2INEPDF"),
            ("empty", "", "2INEPCL;", None),
            ("unknown", "Office;", "2INEPCL;", None),
        ):
            queue_profile(work / "user.profile", user)
            queue_profile(work / "system.profile", system)
            guest.write_bytes(open32_program(queue) if queue else no_default_program())
            output = work / (label + (".pcl" if queue == "2INEPCL" else ".pdf"))
            environment["TWOINE_PRINT_OUTPUT"] = str(output)
            result = subprocess.run(
                [str(build / "lx_loader"), str(guest)],
                cwd=work,
                env=environment,
                capture_output=True,
                timeout=20,
            )
            assert result.returncode == 0, (label, result.returncode, result.stdout, result.stderr)
            if queue == "2INEPDF":
                pdf_pages(output, 1)
            elif queue:
                pcl_pages(output, 1, ink_row=150)  # Open32 coordinates originate at the top.
            else:
                assert not output.exists(), label

        print(
            "Two-page color PDF, PCL raster pages, native/guest queues, buffers, safe host-spool submission and job lifecycle passed"
        )


if __name__ == "__main__":
    main()
