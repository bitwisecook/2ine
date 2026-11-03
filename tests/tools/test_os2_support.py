# Public domain. No rights reserved. Wholly AI-authored.
"""Original-file import and container navigation through mixed media nesting."""

import bz2
import io
import itertools
import json
import struct
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

from tools import ball as ball_api

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
        ball_api.init(self.ball)

    def request(self, action, **fields):
        return ball_api.native_request({"action": action, "ball": str(self.ball), **fields})

    def media(self, name, payload):
        path = self.root / name
        path.write_bytes(payload)
        return str(path)

    def test_unreadable_detected_iso_is_reported_without_hiding_readable_iso(self):
        archive = io.BytesIO()
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as zip:
            zip.writestr("0-broken.iso", b"Invalid ISO")
            zip.writestr("valid.iso", iso_image())
        source = self.media("mixed.zip", archive.getvalue())
        opened = self.request("open-os2-source", sources=[source])
        self.assertEqual(len(opened["sources"]), 1)
        self.assertIn("0-broken.iso", opened["skippedSources"][0]["path"])
        self.assertTrue(opened["directory"]["entries"])
        invalid = self.media("broken.iso", b"Invalid ISO")
        with self.assertRaisesRegex(ValueError, "No readable ISO images"):
            self.request("open-os2-source", sources=[invalid])

    def test_empty_ball_reports_missing_files_and_import_preserves_existing_files_and_drives(self):
        status = self.request("os2-support")
        self.assertFalse(status["complete"])
        self.assertEqual(
            [file["name"] for file in status["files"] if file["required"]],
            ["CMD.EXE", "SYSLEVEL.OS2", "OSO001.MSG"],
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
        remembered = json.loads((self.ball / "os2-media.json").read_text())
        self.assertEqual(remembered["sources"], [media])
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

    def test_media_and_guest_browsers_report_member_sizes_and_dates(self):
        archive = io.BytesIO()
        info = zipfile.ZipInfo("OS2/README.TXT", date_time=(1996, 7, 24, 13, 12, 10))
        with zipfile.ZipFile(archive, "w") as zip:
            zip.writestr(info, b"original media")
        source = self.media("dated.zip", archive.getvalue())
        root = self.request("browse-media", path=source)
        folder = self.request("browse-media", path=root["entries"][0]["path"])
        entry = folder["entries"][0]
        self.assertEqual(entry["size"], 14)
        self.assertGreater(entry["modified"], 800000000)
        self.assertLess(entry["modified"], 900000000)
        target = self.ball / "drive_c/README.TXT"
        target.write_bytes(b"guest file")
        import os

        os.utime(target, (1000000000, 1000000000))
        files = self.request("browse", path="C:\\")["entries"]
        guest = next(file for file in files if file["name"] == "README.TXT")
        self.assertEqual(guest["size"], 10)
        self.assertEqual(guest["modified"], 1000000000)

    def extraction_media(self):
        entries = {
            "OS2/CMD.EXE": CMD,
            "OS2/SYSLEVEL.OS2": LEVEL,
            "OS2/SYSTEM/OSO001.MSG": MESSAGE,
            "OS2/DLL/REXX.DLL": CMD,
            "PSFONTS/TEST.PFB": b"font",
            "OS2/BITMAP/DESKTOP.BMP": b"bitmap",
            "MMOS2/SOUNDS/STARTUP.WAV": b"sound",
            "OS2/DLL/PSCRIPT/PSCRIPT.DRV": b"printer",
            "OS2/DLL/HELPMGR.DLL": CMD,
            "OS2/VIEW.EXE": CMD,
            "OS2/HELP/TEST.HLP": b"help book",
            "OS2/APPS/UTILITY.EXE": CMD,
            "OS2/DLL/PMWIN.DLL": b"provided by 2ine",
            "OS2/CONFIG.SYS": b"never replace config",
        }
        output = io.BytesIO()
        with zipfile.ZipFile(output, "w") as archive:
            for name, data in entries.items():
                archive.writestr(name, data)
        return self.media("extraction.zip", output.getvalue()), entries

    def test_source_detection_opens_iso_inside_nested_archives(self):
        payload = zip_bytes("media.iso", iso_image(payload=CMD, filename="CMD.EXE"))
        source = self.media("outer.zip", zip_bytes("inner.zip", payload))
        opened = self.request("open-os2-source", sources=[source])
        self.assertTrue(opened["detectedISO"])
        self.assertEqual(len(opened["sources"]), 1)
        self.assertIn("media.iso", opened["directory"]["label"])
        plan = self.request("plan-os2-extraction", sources=opened["sources"])
        self.assertEqual([file["path"] for file in plan["files"]], ["C:\\OS2\\CMD.EXE"])

    def test_nested_iso_browsing_uses_lazy_memory_reads_without_materializing_the_image(self):
        image = iso_image(payload=CMD, filename="CMD.EXE") + bytes(64 * 1024 * 1024)
        for method in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
            with self.subTest(method=method):
                output = io.BytesIO()
                with zipfile.ZipFile(output, "w", compression=method) as archive:
                    archive.writestr("media.iso", image)
                source = self.media("lazy.zip", output.getvalue())
                opened = self.request("open-os2-source", sources=[source])
                stats = opened["readStats"]
                self.assertEqual(stats["materializedFiles"], 0)
                self.assertLess(stats["decompressedBytes"], len(image) // 4)
                self.assertLess(stats["peakCacheBytes"], len(image) // 4)
                if method == zipfile.ZIP_STORED:
                    self.assertGreater(stats["directSlices"], 0)
                    self.assertEqual(stats["decompressedBytes"], 0)
                plan = self.request("plan-os2-extraction", sources=opened["sources"])
                self.assertEqual(plan["readStats"]["materializedFiles"], 0)
                self.assertEqual([f["path"] for f in plan["files"]], ["C:\\OS2\\CMD.EXE"])
                result = self.request(
                    "import-os2-support", sources=opened["sources"], categories=["cmd"]
                )
                self.assertEqual(len(result["imported"]), 1)
                (self.ball / "drive_c/OS2/CMD.EXE").unlink()

    def test_extraction_categories_preserve_paths_and_protect_runtime_components(self):
        source, entries = self.extraction_media()
        plan = self.request("plan-os2-extraction", sources=[source])
        classified = {file["path"]: file for file in plan["files"]}
        self.assertEqual(classified["C:\\OS2\\DLL\\REXX.DLL"]["category"], "cmd")
        self.assertEqual(classified["C:\\PSFONTS\\TEST.PFB"]["category"], "fonts")
        self.assertEqual(classified["C:\\OS2\\DLL\\HELPMGR.DLL"]["category"], "help")
        self.assertTrue(classified["C:\\OS2\\DLL\\PMWIN.DLL"]["provided"])
        result = self.request("import-os2-support", sources=[source], categories=["cmd"])
        self.assertEqual(len(result["imported"]), 4)
        self.assertFalse((self.ball / "drive_c/PSFONTS/TEST.PFB").exists())
        self.request("import-os2-support", sources=[source], categories=["fonts", "assets"])
        self.assertEqual((self.ball / "drive_c/PSFONTS/TEST.PFB").read_bytes(), b"font")
        self.assertEqual((self.ball / "drive_c/MMOS2/SOUNDS/STARTUP.WAV").read_bytes(), b"sound")
        self.assertFalse((self.ball / "drive_c/OS2/DLL/HELPMGR.DLL").exists())
        self.request("import-os2-support", sources=[source], categories=["full"])
        for name, data in entries.items():
            if name in ("OS2/DLL/PMWIN.DLL", "OS2/CONFIG.SYS"):
                self.assertFalse((self.ball / "drive_c" / name).exists())
            else:
                self.assertEqual((self.ball / "drive_c" / name).read_bytes(), data)
        self.assertEqual(
            self.request("import-os2-support", sources=[source], categories=["full"])["imported"],
            [],
        )

    def test_browse_folder_and_plan_current_archive_folder(self):
        source, _ = self.extraction_media()
        folder = self.request("browse-media", path=str(self.root))
        self.assertIn("extraction.zip", [entry["name"] for entry in folder["entries"]])
        listing = self.request("browse-media", path=source)
        os2 = next(entry for entry in listing["entries"] if entry["name"] == "OS2")
        plan = self.request("plan-os2-extraction", sources=[os2["path"]])
        self.assertTrue(plan["files"])
        self.assertTrue(all(file["path"].startswith("C:\\OS2\\") for file in plan["files"]))

    def test_conflicting_versions_fail_before_any_selected_files_are_installed(self):
        one = self.media("one.zip", zip_bytes("PSFONTS/Test.pfb", b"first"))
        two = self.media("two.zip", zip_bytes("PSFONTS/test.PFB", b"second"))
        with self.assertRaisesRegex(ValueError, "Different versions"):
            self.request("import-os2-support", sources=[one, two], categories=["fonts"])
        self.assertFalse((self.ball / "drive_c/PSFONTS").exists())
        with self.assertRaisesRegex(ValueError, "Select at least"):
            self.request("import-os2-support", sources=[one], categories=[])
        self.assertFalse(list(self.ball.glob(".2ine-*")))

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

    def test_rexx_runtime_is_imported_into_dll_system_and_command_directories(self):
        members = {
            "REXX.DLL": CMD,
            "REXXAPI.DLL": CMD,
            "REXXUTIL.DLL": CMD,
            "REXXINIT.DLL": CMD,
            "REX.MSG": MESSAGE,
            "REXH.MSG": MESSAGE,
            "RXQUEUE.EXE": CMD,
            "RXSUBCOM.EXE": CMD,
        }
        payload = io.BytesIO()
        with zipfile.ZipFile(payload, "w") as archive:
            for name, data in members.items():
                archive.writestr("OS2/" + name, pack2("OS2/" + name, data))
        result = self.request(
            "import-os2-support", sources=[self.media("rexx.zip", payload.getvalue())]
        )
        self.assertTrue(result["rexxComplete"])
        self.assertFalse(result["complete"])
        self.assertEqual(len(result["imported"]), len(members))
        for name, data in members.items():
            directory = (
                "OS2/DLL"
                if name.endswith(".DLL")
                else "OS2/SYSTEM"
                if name.endswith(".MSG")
                else "OS2"
            )
            self.assertEqual((self.ball / "drive_c" / directory / name).read_bytes(), data)
        base = [f for f in result["files"] if f["required"]]
        self.assertEqual([f["name"] for f in base], ["CMD.EXE", "SYSLEVEL.OS2", "OSO001.MSG"])

    def test_rexx_command_reports_the_missing_original_interpreter_before_launch(self):
        directory = self.ball / "drive_c/OS2"
        (directory / "CMD.EXE").write_bytes(CMD)
        (directory / "SCRIPT.CMD").write_text("/* REXX */\nsay 'Hello'\n")
        build = self.root / "runtime"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"loader")
        with self.assertRaisesRegex(ValueError, "original OS/2 REXX interpreter"):
            self.request(
                "prepare-launch", build=str(build), arguments=["run", "C:\\OS2\\SCRIPT.CMD"]
            )

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
