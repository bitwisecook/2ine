# Public domain. No rights reserved. Wholly AI-authored.
"""Original-file import and container navigation through mixed media nesting."""

import bz2
import io
import itertools
import struct
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

from tools import balls

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))
from disk_images import fat_image, iso_image

CMD = b"MZ" + bytes(58) + struct.pack("<I", 64) + b"LX" + bytes(34)
LEVEL = b"\xff\xffSYSLEVEL" + bytes(155)
MESSAGE = b"\xffMKMSGF\0" + bytes(100)


def zip_bytes(name, payload):
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr(name, payload)
    return output.getvalue()


def pack2(name, payload, next_member=0):
    name = name.encode() + b"\0"
    header = bytearray(41)
    header[:4] = bytes.fromhex("a596fdff")
    struct.pack_into("<III", header, 12, 0, len(payload), next_member)
    header[24:31] = b"FTCOMP\0"
    struct.pack_into("<H", header, 39, len(name))
    return header + name + bytes(4) + payload  # PACK2's stored stream form.


def wpi(name, payload, compressed=True):
    data = bz2.compress(payload) if compressed else payload
    header = bytearray(532)
    header[:4] = bytes.fromhex("770402be")
    struct.pack_into("<H", header, 4, 4)
    struct.pack_into("<H", header, 522, 1)
    extended = struct.pack("<10I", 40, 0, *([0] * 8))
    package = struct.pack("<HHIII32s", 1, 1, 532 + 40 + 48, len(payload), len(data), b"Test")
    file = struct.pack(
        "<HHHHIII256sIIB",
        0xF012,
        0,
        int(compressed),
        1,
        len(payload),
        len(data),
        0,
        name.encode(),
        0,
        0,
        0,
    )
    return header + extended + package + file + data


class OS2SupportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="2ine-support-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.ball = self.root / "Ball"
        balls.init(self.ball)

    def request(self, action, **fields):
        return balls.native_request({"action": action, "ball": str(self.ball), **fields})

    def media(self, name, payload):
        path = self.root / name
        path.write_bytes(payload)
        return str(path)

    def test_empty_ball_reports_missing_files_and_import_preserves_existing_files_and_drives(self):
        status = self.request("os2-support")
        self.assertFalse(status["complete"])
        self.assertEqual(
            [file["name"] for file in status["files"]], ["CMD.EXE", "SYSLEVEL.OS2", "OSO001.MSG"]
        )
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, "w") as zip:
            for name, data in (
                ("OS2/CMD.EXE", CMD),
                ("OS2/SYSLEVEL.OS2", LEVEL),
                ("OS2/SYSTEM/OSO001.MSG", MESSAGE),
            ):
                zip.writestr(name, data)
        media = self.media("OS2.zip", archive.getvalue())
        manifest = (self.ball / "ball.json").read_bytes()
        status = self.request("import-os2-support", sources=[media])
        self.assertTrue(status["complete"])
        self.assertEqual(len(status["imported"]), 3)
        self.assertEqual(self.request("guest-environment")["COMSPEC"], "C:\\OS2\\CMD.EXE")
        self.assertEqual(
            self.request("guest-environment", environment={"COMSPEC": "D:\\CUSTOM\\CMD.EXE"})[
                "COMSPEC"
            ],
            "D:\\CUSTOM\\CMD.EXE",
        )
        self.assertEqual((self.ball / "drive_c/OS2/CMD.EXE").read_bytes(), CMD)
        self.assertEqual((self.ball / "drive_c/OS2/SYSTEM/OSO001.MSG").read_bytes(), MESSAGE)
        self.assertEqual((self.ball / "ball.json").read_bytes(), manifest)
        self.assertEqual(self.request("import-os2-support", sources=[media])["imported"], [])

    def test_zip_pack2_iso_can_be_browsed_in_every_order(self):
        wrappers = {
            "zip": zip_bytes,
            "pack2": pack2,
            "iso": lambda name, data: iso_image(payload=data, filename=name),
        }
        for order in itertools.permutations(wrappers):
            with self.subTest(order=order):
                name, payload = "CMD.EXE", CMD
                for kind in reversed(order):
                    payload = wrappers[kind](name, payload)
                    name = "MEDIA." + kind
                path = self.media(name, payload)
                for kind in order:
                    listing = self.request("browse-media", path=path)
                    if kind == "iso":
                        listing = self.request("browse-media", path=listing["entries"][0]["path"])
                    self.assertEqual(len(listing["entries"]), 1)
                    path = listing["entries"][0]["path"]
                self.assertEqual(listing["entries"][0]["name"], "CMD.EXE")
                self.request("import-os2-support", sources=[path])
                self.assertEqual((self.ball / "drive_c/OS2/CMD.EXE").read_bytes(), CMD)
                (self.ball / "drive_c/OS2/CMD.EXE").unlink()

    def test_nesting_has_no_fixed_depth_limit_or_exponential_path_growth(self):
        payload, name = CMD, "CMD.EXE"
        for _ in range(40):
            payload = zip_bytes(name, payload)
            name = "inner.zip"
        path = self.media("deep.zip", payload)
        for _ in range(40):
            listing = self.request("browse-media", path=path)
            path = listing["entries"][0]["path"]
        self.assertLess(len(path), 3000)
        status = self.request("import-os2-support", sources=[path])
        self.assertTrue(status["files"][0]["present"])

    def test_tar_gzip_wpi_and_floppy_inside_zip(self):
        for compressed in (False, True):
            payload = wpi("data/CMD.EXE", CMD, compressed)
            path = self.media("support.wpi", payload)
            listing = self.request("browse-media", path=path)
            inner = self.request("browse-media", path=listing["entries"][0]["path"])
            self.assertEqual(inner["entries"][0]["name"], "CMD.EXE")
            self.request("import-os2-support", sources=[path])
            self.assertEqual((self.ball / "drive_c/OS2/CMD.EXE").read_bytes(), CMD)
        output = io.BytesIO()
        with tarfile.open(fileobj=output, mode="w:gz") as archive:
            data = zip_bytes("system.pack2", pack2("SYSLEVEL.OS2", LEVEL))
            info = tarfile.TarInfo("nested.zip")
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))
        self.request("import-os2-support", sources=[self.media("support.tgz", output.getvalue())])
        self.assertEqual((self.ball / "drive_c/OS2/SYSLEVEL.OS2").read_bytes(), LEVEL)
        floppy = self.media("disk.zip", zip_bytes("disk.img", fat_image()))
        listing = self.request("browse-media", path=floppy)
        disk = self.request("browse-media", path=listing["entries"][0]["path"])
        self.assertIn("INSTALL", [entry["name"] for entry in disk["entries"]])

    def test_unsafe_archive_paths_and_symlinks_are_not_imported(self):
        output = io.BytesIO()
        with zipfile.ZipFile(output, "w") as archive:
            archive.writestr("../CMD.EXE", CMD)
            archive.writestr("C:/SYSLEVEL.OS2", LEVEL)
        path = self.media("unsafe.zip", output.getvalue())
        status = self.request("import-os2-support", sources=[path])
        self.assertFalse(status["complete"])
        self.assertEqual(status["imported"], [])
        self.assertEqual(self.request("browse-media", path=path)["entries"], [])

    def test_conflicting_releases_and_invalid_payloads_are_rejected_before_installing(self):
        first = self.media("first.zip", zip_bytes("CMD.EXE", CMD))
        second = self.media("second.zip", zip_bytes("CMD.EXE", CMD + b"different"))
        with self.assertRaisesRegex(ValueError, "Different versions"):
            self.request("import-os2-support", sources=[first, second])
        self.assertFalse((self.ball / "drive_c/OS2/CMD.EXE").exists())
        bad = self.media("bad.zip", zip_bytes("CMD.EXE", b"not an executable"))
        with self.assertRaisesRegex(ValueError, "not an original OS/2"):
            self.request("import-os2-support", sources=[bad])

    def test_compressed_support_file_with_its_final_name_is_unwrapped(self):
        media = self.media("CMD.EXE", pack2("CMD.EXE", CMD))
        result = self.request("import-os2-support", sources=[media])
        self.assertTrue(result["files"][0]["present"])
        self.assertEqual((self.ball / "drive_c/OS2/CMD.EXE").read_bytes(), CMD)
        listing = self.request("browse-media", path=media)
        self.assertFalse(listing["entries"][0]["container"])

    def test_missing_and_corrupt_selected_media_are_reported(self):
        for source in (
            str(self.root / "missing.iso"),
            self.media("corrupt.zip", b"invalid archive"),
        ):
            with (
                self.subTest(source=source),
                self.assertRaisesRegex(ValueError, "Cannot read selected"),
            ):
                self.request("import-os2-support", sources=[source])

    def test_smartsuite_executable_is_checked_before_launch(self):
        drive = self.ball / "drive_c/OS2"
        (drive / "INSTALL.EXE").write_bytes(CMD)
        (drive / "SUITE.RSP").write_text("response")
        build = self.root / "build"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"loader")
        with self.assertRaisesRegex(ValueError, "SmartSuite needs original OS/2 support"):
            self.request(
                "prepare-launch", build=str(build), arguments=["install", "C:\\OS2\\INSTALL.EXE"]
            )


if __name__ == "__main__":
    unittest.main()
