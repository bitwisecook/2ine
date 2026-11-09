# Public domain. No rights reserved. Wholly AI-authored.
"""ZIP interoperability, media portability and non-replacing ball imports."""

import contextlib
import hashlib
import io
import json
import os
import stat
import tempfile
import unittest
import warnings
import zipfile
from pathlib import Path

from tools import ball as ball_api


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="2ine-archives-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.ball = self.root / "Source café.2ineball"
        self.archive = self.root / "Ball café.zip"
        self.destination = self.root / "Imported café.2ineball"
        ball_api.init(self.ball)
        self.document = self.ball / "drive_c/document.txt"
        self.document.write_bytes(b"installed document\x00\xff")
        self.image = self.root / "external.img"
        self.request("command", arguments=["create-floppy", "A", str(self.image), "1440"])
        self.internal = self.ball / "spare.iso"
        self.internal.write_bytes(b"unattached image")
        self.original = (self.ball / "ball.json").read_bytes()

    def request(self, action, **kwargs):
        return ball_api.native_request({"action": action, "ball": str(self.ball), **kwargs})

    def export(self, images=False):
        return self.request("export-ball", path=str(self.archive), includeImages=images)

    def import_ball(self):
        return ball_api.native_request(
            {"action": "import-ball", "ball": str(self.destination), "path": str(self.archive)}
        )

    def test_without_images_roundtrips_files_unknown_metadata_and_ejected_devices(self):
        data = json.loads(self.original)
        data["custom"] = {"large": 9223372036854775807, "nested": [True, "café"]}
        (self.ball / "ball.json").write_text(json.dumps(data))
        self.export()
        with zipfile.ZipFile(self.archive) as archive:
            self.assertIsNone(archive.testzip())
            self.assertNotIn("spare.iso", archive.namelist())
            self.assertFalse(
                any(name.startswith(("devices/", "ipc/")) for name in archive.namelist())
            )
        # An image left out of the package stays as a reconnect hint.
        hint = str(self.image)
        self.image.unlink()
        result = self.import_ball()
        self.assertEqual(
            (self.destination / "drive_c/document.txt").read_bytes(), self.document.read_bytes()
        )
        self.assertEqual(result["manifest"]["drives"]["A"]["image"], hint)
        self.assertEqual(result["manifest"]["drives"]["A"]["device"], "floppy")
        self.assertEqual(result["unavailableDrives"]["A"], hint)
        self.assertEqual(result["manifest"]["references"]["drive-A"]["path"], hint)
        self.assertEqual(result["manifest"]["custom"], data["custom"])
        self.assertEqual(result["roots"]["C"], str(self.destination / "drive_c"))

    def test_bundled_images_work_after_original_ball_and_media_are_removed(self):
        self.export(images=True)
        with zipfile.ZipFile(self.archive) as archive:
            self.assertIsNone(archive.testzip())
            manifest = json.loads(archive.read("ball.json"))
            image = manifest["drives"]["A"]["image"]
            self.assertFalse(Path(image).is_absolute())
            self.assertEqual(archive.read(image), self.image.read_bytes())
            self.assertEqual(archive.read("spare.iso"), b"unattached image")
        self.assertEqual((self.ball / "ball.json").read_bytes(), self.original)
        self.image.unlink()
        self.ball.rename(self.root / "Moved.2ineball")
        result = self.import_ball()
        listing = ball_api.native_request(
            {"action": "browse", "ball": str(self.destination), "path": "A:\\"}
        )
        self.assertEqual(listing["entries"], [])
        self.assertTrue((self.destination / result["manifest"]["drives"]["A"]["image"]).is_file())
        # Re-exporting imported relative image mappings must also work.
        ball_api.native_request(
            {
                "action": "export-ball",
                "ball": str(self.destination),
                "path": str(self.root / "again.zip"),
                "includeImages": True,
            }
        )

    def test_referenced_internal_images_are_bundled_once_even_with_unknown_extension(self):
        image = self.ball / "private-media.bin"
        self.image.rename(image)
        self.request("command", arguments=["map-image", "A", str(image)])
        self.export(images=True)
        with zipfile.ZipFile(self.archive) as archive:
            self.assertNotIn("private-media.bin", archive.namelist())
            self.assertIn("bundled-media/A.bin", archive.namelist())
        self.archive.unlink()
        self.export()
        with zipfile.ZipFile(self.archive) as archive:
            self.assertNotIn("private-media.bin", archive.namelist())

    def test_omitting_images_allows_missing_media_but_bundling_fails_atomically(self):
        self.image.unlink()
        self.export()
        original = self.archive.read_bytes()
        with self.assertRaises(ValueError):
            self.export(images=True)
        self.assertEqual(self.archive.read_bytes(), original)
        self.assertFalse(list(self.root.glob(".2ine-export-*")))

    def test_import_never_replaces_existing_ball_or_symlink(self):
        self.export()
        self.destination.mkdir()
        marker = self.destination / "keep"
        marker.write_bytes(b"keep")
        with self.assertRaisesRegex(ValueError, "existing items"):
            self.import_ball()
        self.assertEqual(marker.read_bytes(), b"keep")
        marker.unlink()
        self.destination.rmdir()
        if os.name != "nt":
            self.destination.symlink_to(self.root / "absent")
            with self.assertRaises(ValueError):
                self.import_ball()
            self.assertTrue(self.destination.is_symlink())

    def test_rejects_unsafe_zip_paths_and_cleans_failed_imports(self):
        for name in (
            "../escaped",
            "/absolute",
            "C:/absolute",
            "drive_c/../../escaped",
            "drive_c/..\\escaped",
            "drive_c/alias.",
            "drive_c/NUL.txt",
            "drive_c/COM1",
        ):
            with self.subTest(name=name):
                with zipfile.ZipFile(self.archive, "w") as archive:
                    archive.writestr("ball.json", self.original)
                    archive.writestr(name, b"unsafe")
                with self.assertRaises(ValueError):
                    self.import_ball()
                self.assertFalse(self.destination.exists())
                self.assertFalse((self.root / "escaped").exists())

    def test_rejects_symbolic_links_and_duplicate_files(self):
        with zipfile.ZipFile(self.archive, "w") as archive:
            link = zipfile.ZipInfo("drive_c/link")
            link.create_system = 3
            link.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(link, "../../outside")
        with self.assertRaisesRegex(ValueError, "Unsafe"):
            self.import_ball()
        with warnings.catch_warnings(), zipfile.ZipFile(self.archive, "w") as archive:
            warnings.simplefilter("ignore", UserWarning)
            archive.writestr("ball.json", self.original)
            archive.writestr("drive_c/file", b"one")
            archive.writestr("drive_c/FILE" if os.name == "nt" else "drive_c/file", b"two")
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.import_ball()
        self.assertFalse(self.destination.exists())

    def test_invalid_manifest_or_zip_is_removed(self):
        for contents in (b"garbage", b"", b"PK\x03\x04truncated"):
            self.archive.write_bytes(contents)
            with self.assertRaises(ValueError):
                self.import_ball()
            self.assertFalse(self.destination.exists())
        for manifest in (
            {"version": 9},
            {"version": 1, "drives": {"C": str(self.root)}},
            {"version": 1, "drives": {"C": "../outside"}},
        ):
            with zipfile.ZipFile(self.archive, "w") as archive:
                archive.writestr("ball.json", json.dumps(manifest))
            with self.assertRaises(ValueError):
                self.import_ball()
            self.assertFalse(self.destination.exists())

    def test_corrupt_zip_payload_is_rejected_and_removed(self):
        with zipfile.ZipFile(self.archive, "w", compression=zipfile.ZIP_STORED) as archive:
            archive.writestr("ball.json", self.original)
        payload = bytearray(self.archive.read_bytes())
        position = payload.find(self.original)
        self.assertGreater(position, 0)
        payload[position] ^= 1
        self.archive.write_bytes(payload)
        with self.assertRaises(ValueError):
            self.import_ball()
        self.assertFalse(self.destination.exists())

    @unittest.skipIf(os.name == "nt", "POSIX executable permissions")
    def test_executable_permissions_survive_transfer(self):
        self.document.chmod(0o751)
        self.export()
        self.import_ball()
        self.assertEqual((self.destination / "drive_c/document.txt").stat().st_mode & 0o777, 0o751)

    def test_export_rejects_output_inside_ball_including_symlinked_parent(self):
        with self.assertRaisesRegex(ValueError, "outside the ball"):
            self.request("export-ball", path=str(self.ball / "out.zip"))
        if os.name != "nt":
            alias = self.root / "alias"
            alias.symlink_to(self.ball, target_is_directory=True)
            with self.assertRaises(ValueError):
                self.request("export-ball", path=str(alias / "out.zip"))
        self.assertFalse((self.ball / "out.zip").exists())

    @unittest.skipIf(os.name == "nt", "POSIX symlink fixture")
    def test_export_rejects_links_and_preserves_existing_archive(self):
        self.archive.write_bytes(b"existing archive")
        (self.ball / "link").symlink_to(self.document)
        with self.assertRaisesRegex(ValueError, "symbolic link"):
            self.export()
        self.assertEqual(self.archive.read_bytes(), b"existing archive")
        self.assertFalse(list(self.root.glob(".2ine-export-*")))

    def test_cli_import_and_export(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(
                ball_api.main(
                    [
                        "--ball",
                        str(self.ball),
                        "--build",
                        str(self.root),
                        "export",
                        str(self.archive),
                        "--include-images",
                    ]
                ),
                0,
            )
            self.assertEqual(
                ball_api.main(["--ball", str(self.destination), "import", str(self.archive)]), 0
            )
        self.assertTrue((self.destination / "ball.json").is_file())

    def test_export_records_engine_version_and_actual_selected_runtime_identity(self):
        runtime = self.root / "Runtime"
        runtime.mkdir()
        loader = runtime / "lx_loader"
        loader.write_bytes(b"runtime build one")
        (runtime / "libtest.so").write_bytes(b"runtime library")
        details = {
            "sourceVersion": "v2.0-test",
            "sourceRevision": "0123456789",
            "configuration": "Debug",
        }
        (runtime / "2ine-build.json").write_text(json.dumps(details))
        self.request(
            "export-ball",
            path=str(self.archive),
            build=str(runtime),
            exporter={"version": "1.2", "build": "42"},
        )
        with zipfile.ZipFile(self.archive) as archive:
            info = json.loads(archive.read("2ine-export.json"))
        self.assertEqual(info["engine"]["name"], "2ine")
        self.assertTrue(info["engine"]["sourceVersion"])
        self.assertTrue(info["engine"]["sourceRevision"])
        self.assertEqual(info["exporter"], {"version": "1.2", "build": "42"})
        self.assertEqual(info["runtime"]["buildInformation"], details)
        self.assertTrue(info["runtime"]["available"])
        self.assertEqual(len(info["runtime"]["identitySHA256"]), 64)

        self.assertEqual(
            info["runtime"]["loaderSHA256"], hashlib.sha256(loader.read_bytes()).hexdigest()
        )
        loader.write_bytes(b"runtime build two")
        self.request("export-ball", path=str(self.archive), build=str(runtime))
        with zipfile.ZipFile(self.archive) as archive:
            updated = json.loads(archive.read("2ine-export.json"))
        self.assertNotEqual(info["runtime"]["identitySHA256"], updated["runtime"]["identitySHA256"])
        self.import_ball()
        self.assertEqual(json.loads((self.destination / "2ine-export.json").read_bytes()), updated)

    def test_desktop_wrapper_cache_is_not_exported_or_reused_on_import(self):
        desktop = self.ball / "cache/desktop-sessions/fixture/Program.app/Contents/Resources"
        desktop.mkdir(parents=True)
        (desktop / "Runtime").symlink_to("../../../../../drive_c/OS2/DLL/2INE")
        (self.ball / "cache/keep.txt").write_text("unrelated cache is preserved")
        self.export()
        with zipfile.ZipFile(self.archive) as archive:
            self.assertFalse(
                any(name.startswith("cache/desktop-sessions/") for name in archive.namelist())
            )
            self.assertIn("cache/keep.txt", archive.namelist())
        # Older exports may contain ordinary wrapper files; they must be reset on import.
        with zipfile.ZipFile(self.archive, "a") as archive:
            archive.writestr(
                "cache/desktop-sessions/old/Program.app/Contents/MacOS/lx_loader",
                b"old host loader",
            )
        self.import_ball()
        self.assertFalse((self.destination / "cache/desktop-sessions").exists())
        self.assertEqual(
            (self.destination / "cache/keep.txt").read_text(), "unrelated cache is preserved"
        )

    def test_internal_mounts_are_relative_and_host_runtime_is_reselected(self):
        extra = self.ball / "extra"
        extra.mkdir()
        (extra / "data.txt").write_bytes(b"portable")
        data = json.loads(self.original)
        data["drives"]["C"] = str(self.ball / "drive_c")
        data["drives"]["E"] = str(extra)
        data["runtime"]["build"] = str(self.root / "host-runtime")
        cache = self.ball / "drive_c/OS2/DLL/2INE/native/old"
        cache.mkdir(parents=True)
        (cache / "libhost.so").write_bytes(b"host-specific")
        (self.ball / "ball.json").write_text(json.dumps(data))
        self.export()
        with zipfile.ZipFile(self.archive) as archive:
            manifest = json.loads(archive.read("ball.json"))
            self.assertEqual(manifest["drives"]["C"], "drive_c")
            self.assertEqual(manifest["drives"]["E"], "extra")
            self.assertNotIn("build", manifest["runtime"])
            self.assertFalse(any("/DLL/2INE/" in name for name in archive.namelist()))
            metadata = json.loads(archive.read("2ine-export.json"))
            self.assertEqual(metadata["runtime"]["settings"]["build"], data["runtime"]["build"])
        self.ball.rename(self.root / "moved")
        result = self.import_ball()
        self.assertEqual(result["roots"]["E"], str(self.destination / "extra"))
        self.assertEqual((self.destination / "extra/data.txt").read_bytes(), b"portable")

    def test_missing_folder_imports_ejected_and_reconnects_without_losing_guest_settings(self):
        external = self.root / "external-folder"
        external.mkdir()
        self.request("command", arguments=["map", "E", str(external)])
        data = ball_api.load(self.ball)
        data["workingDirectories"] = {"E": "E:\\DATA"}
        data["currentDrive"] = "E"
        data["environment"]["LIBPATH"] = "C:\\OS2\\DLL;E:\\DLL"
        data["applications"]["Editor"] = {
            "program": "E:\\APPS\\EDITOR.EXE",
            "arguments": ["a b"],
            "cwd": "E:\\APPS",
        }
        (self.ball / "ball.json").write_text(json.dumps(data))
        self.export()
        external.rmdir()
        result = self.import_ball()
        self.assertEqual(result["unavailableDrives"]["E"], str(external))
        self.assertNotIn("E", result["roots"])
        self.assertEqual(result["workingDirectories"]["E"], "E:\\DATA")
        self.assertEqual(result["manifest"]["currentDrive"], "E")
        self.assertEqual(result["manifest"]["applications"], data["applications"])
        self.assertIn("mountpoint.E = \n", (self.destination / "2ine.cfg").read_text())
        with self.assertRaisesRegex(ValueError, "ejected"):
            ball_api.native_request(
                {"action": "browse", "ball": str(self.destination), "path": "E:\\"}
            )
        ball_api.environment(self.destination, result["manifest"], self.root, "unicorn")
        replacement = self.root / "replacement"
        (replacement / "DATA").mkdir(parents=True)
        updated = ball_api.native_request(
            {
                "action": "command",
                "ball": str(self.destination),
                "arguments": ["map", "E", str(replacement)],
            }
        )
        self.assertEqual(updated["roots"]["E"], str(replacement))
        self.assertNotIn("E", updated["unavailableDrives"])
        self.assertEqual(updated["workingDirectories"]["E"], "E:\\DATA")
        reopened = ball_api.load(self.destination)
        self.assertEqual(reopened["workingDirectories"], {"E": "E:\\DATA"})
        self.assertEqual(reopened["applications"], data["applications"])

    def test_missing_folder_automatically_reconnects_when_path_returns(self):
        external = self.root / "external-folder"
        external.mkdir()
        self.request("command", arguments=["map", "E", str(external)])
        external.rmdir()
        result = self.request("snapshot")
        self.assertEqual(result["unavailableDrives"]["E"], str(external))
        self.export()
        external.mkdir()
        result = self.request("snapshot")
        self.assertEqual(result["roots"]["E"], str(external))
        self.assertNotIn("E", result["unavailableDrives"])

    def test_foreign_host_paths_and_legacy_relative_separators_import_as_ejected(self):
        data = json.loads(self.original)
        data["drives"]["C"] = ".\\drive_c\\"
        foreign = "/Volumes/Absent/Folder" if os.name == "nt" else "Z:\\Absent\\Folder"
        data["drives"]["E"] = foreign
        data["runtime"]["build"] = foreign
        with zipfile.ZipFile(self.archive, "w") as archive:
            archive.writestr("ball.json", json.dumps(data))
            archive.writestr("drive_c/CONFIG.SYS", b"SET TEST=portable\r\n")
            archive.writestr("drive_c/OS2/DLL/2INE/old/libhost.so", b"old binary")
        result = self.import_ball()
        self.assertNotIn("E", result["roots"])
        self.assertEqual(result["unavailableDrives"]["E"], foreign.replace("\\", "/"))
        self.assertNotIn("build", result["manifest"]["runtime"])
        self.assertFalse((self.destination / "drive_c/OS2/DLL/2INE").exists())
        self.assertEqual(result["configSys"], "SET TEST=portable\r\n")

    def test_missing_image_source_is_ejected_without_losing_its_path_hint(self):
        self.image.unlink()
        result = self.request("snapshot")
        self.assertEqual(result["unavailableDrives"]["A"], str(self.image))
        self.assertEqual(result["manifest"]["drives"]["A"]["image"], str(self.image))
        with self.assertRaisesRegex(ValueError, "No media is inserted"):
            self.request("browse", path="A:\\")


if __name__ == "__main__":
    unittest.main()
