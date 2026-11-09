# Public domain. No rights reserved. Wholly AI-authored.
"""References to images, folders and media outside a ball: states and relinking."""

import json
import os
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

from tools.ball import engine as ball_api

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "integration"))
from disk_images import fat_image, iso_image


class ReferenceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="2ine-references-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        previous = os.environ.get("XDG_CONFIG_HOME")
        self.addCleanup(
            lambda: (
                os.environ.pop("XDG_CONFIG_HOME", None)
                if previous is None
                else os.environ.__setitem__("XDG_CONFIG_HOME", previous)
            )
        )
        os.environ["XDG_CONFIG_HOME"] = str(self.root / "config")
        self.ball = self.root / "Refs.2ineball"
        ball_api.init(self.ball)
        self.media = self.root / "media"
        self.media.mkdir()
        self.iso = self.media / "disc.iso"
        self.iso.write_bytes(iso_image())
        self.floppy = self.media / "disk.img"
        self.floppy.write_bytes(fat_image())

    def request(self, action, **fields):
        return ball_api.native_request({"action": action, "ball": str(self.ball), **fields})

    def command(self, *arguments):
        return self.request("command", arguments=list(arguments))

    def states(self, **fields):
        status = self.request("reference-status", **fields)["references"]
        return {name: record["state"] for name, record in status.items()}

    def test_drive_mappings_mirror_into_reference_records_with_identity_hints(self):
        self.command("map-image", "D", str(self.iso))
        self.command("map", "E", str(self.media))
        manifest = json.loads((self.ball / "ball.json").read_text())
        self.assertEqual(manifest["version"], 1)
        self.assertEqual(manifest["drives"]["D"]["image"], str(self.iso))
        image = manifest["references"]["drive-D"]
        self.assertEqual(
            (image["kind"], image["base"], image["path"]), ("image", "absolute", str(self.iso))
        )
        self.assertEqual(image["hint"]["size"], self.iso.stat().st_size)
        self.assertEqual(image["hint"]["volumeLabel"], "TEST_MEDIA")
        self.assertTrue(image["readonly"])
        self.assertEqual(manifest["references"]["drive-E"]["kind"], "folder")
        self.command("unmap", "E")
        self.command("eject", "D")
        manifest = json.loads((self.ball / "ball.json").read_text())
        self.assertNotIn("references", manifest)
        self.assertEqual(self.states(), {"drive-D": "ejected"})

    def test_missing_mismatched_and_foreign_references_never_move_by_themselves(self):
        self.command("map-image", "A", str(self.floppy))
        self.command("map-image", "D", str(self.iso))
        self.assertEqual(self.states(), {"drive-A": "available", "drive-D": "available"})
        self.floppy.write_bytes(fat_image(16))
        moved = self.root / "moved"
        moved.mkdir()
        self.iso.rename(moved / "disc.iso")
        self.assertEqual(self.states(), {"drive-A": "mismatch", "drive-D": "missing"})
        found = self.request("search-references", searchRoots=[str(self.root)])["references"]
        self.assertEqual(found["drive-D"]["state"], "relocation-candidate")
        self.assertEqual(found["drive-D"]["candidate"], str(moved / "disc.iso"))
        manifest = json.loads((self.ball / "ball.json").read_text())
        self.assertEqual(manifest["drives"]["D"]["image"], str(self.iso))
        data = manifest
        data["drives"]["D"]["image"] = "C:\\Media\\disc.iso"
        data["references"]["drive-D"]["path"] = "C:\\Media\\disc.iso"
        (self.ball / "ball.json").write_text(json.dumps(data))
        if os.name != "nt":
            self.assertEqual(self.states()["drive-D"], "foreign")

    def test_the_host_remap_table_offers_moved_libraries_as_candidates(self):
        self.command("map-image", "D", str(self.iso))
        library = self.root / "library"
        self.media.rename(library)
        self.assertEqual(self.states(), {"drive-D": "missing"})
        table = self.request("reference-remaps", **{"from": str(self.media), "to": str(library)})
        self.assertEqual(Path(table["path"]), self.root / "config/2ine/remap.json")
        status = self.request("reference-status")["references"]["drive-D"]
        self.assertEqual(status["state"], "relocation-candidate")
        self.assertEqual(status["candidate"], str(library / "disc.iso"))
        self.request("relink-reference", id="drive-D", path=status["candidate"], hash=True)
        manifest = json.loads((self.ball / "ball.json").read_text())
        self.assertEqual(manifest["drives"]["D"]["image"], str(library / "disc.iso"))
        self.assertEqual(len(manifest["references"]["drive-D"]["hint"]["sha256"]), 64)
        self.assertEqual(self.states(verify=True), {"drive-D": "available"})

    def test_relink_respects_locked_slots(self):
        self.command("map-image", "D", str(self.iso))
        self.command("device-lock", "D", "on")
        with self.assertRaisesRegex(ValueError, "locked"):
            self.request("relink-reference", id="drive-D", path=str(self.floppy))

    def test_bundle_and_unbundle_move_an_image_in_and_out_explicitly(self):
        self.command("map-image", "A", str(self.floppy))
        self.request("bundle-reference", id="drive-A")
        manifest = json.loads((self.ball / "ball.json").read_text())
        self.assertEqual(manifest["drives"]["A"]["image"], "bundled-media/A.img")
        self.assertEqual(manifest["references"]["drive-A"]["base"], "ball")
        self.assertTrue(self.floppy.is_file())
        renamed = self.request("rename-ball", text="Moved")["path"]
        self.ball = Path(renamed)
        self.assertEqual(self.states(), {"drive-A": "available"})
        target = self.root / "out.img"
        self.request("unbundle-reference", id="drive-A", path=str(target))
        self.assertTrue(target.is_file())
        self.assertFalse((self.ball / "bundled-media/A.img").exists())
        manifest = json.loads((self.ball / "ball.json").read_text())
        self.assertEqual(manifest["references"]["drive-A"]["path"], str(target))

    def test_host_data_and_reconnect_hints_in_exported_packages(self):
        self.command("map-image", "D", str(self.iso))
        self.request("set-reference-host-data", id="drive-D", host="mac", data={"bookmark": "x"})
        manifest = json.loads((self.ball / "ball.json").read_text())
        self.assertEqual(manifest["references"]["drive-D"]["host"], {"mac": {"bookmark": "x"}})
        package = self.root / "out.zip"
        self.request("export-ball", path=str(package))
        with zipfile.ZipFile(package) as archive:
            exported = json.loads(archive.read("ball.json"))
        self.assertEqual(exported["drives"]["D"]["image"], str(self.iso))
        self.assertNotIn("host", exported["references"]["drive-D"])

    def test_remembered_media_are_references_that_follow_a_relink(self):
        self.request("remember-os2-media", sources=[str(self.iso)])
        manifest = json.loads((self.ball / "ball.json").read_text())
        self.assertEqual(manifest["media"], ["media-1"])
        moved = self.root / "elsewhere.iso"
        self.iso.rename(moved)
        self.assertEqual(self.states()["media-1"], "missing")
        self.request("relink-reference", id="media-1", path=str(moved))
        remembered = json.loads((self.ball / "os2-media.json").read_text())
        self.assertEqual(remembered["sources"], [str(moved)])
        self.assertEqual(self.states()["media-1"], "available")

    def test_reading_a_ball_leaves_device_control_files_untouched(self):
        self.command("map-image", "D", str(self.iso))
        control = self.ball / "devices/D.media"
        before = control.stat().st_mtime_ns
        os.utime(control, ns=(before - 10**9, before - 10**9))
        stamped = control.stat().st_mtime_ns
        for action in ("snapshot", "browse", "reference-status"):
            self.request(action)
        self.assertEqual(control.stat().st_mtime_ns, stamped)


if __name__ == "__main__":
    unittest.main()
