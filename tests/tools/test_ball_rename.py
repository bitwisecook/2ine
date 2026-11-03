# Public domain. No rights reserved. Wholly AI-authored.
"""Atomic package renames preserve guest state and rebase owned host paths."""

import hashlib
import os
import tempfile
import unittest
from pathlib import Path

from tools import ball as ball_api


class RenameTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="2ine-rename-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.ball = self.root / "Old café.2ineball"
        ball_api.init(self.ball)
        self.protected = {
            "drive_c/CONFIG.SYS": b"REM caf\xe9\r\nSET PATH=C:\\OS2\r\n",
            "drive_c/OS2/OS2.INI": b"opaque user profile\0\xff",
            "drive_c/OS2/OS2SYS.INI": b"opaque system profile\x80",
            "drive_c/document.txt": b"guest document\0\xff",
        }
        for name, content in self.protected.items():
            (self.ball / name).write_bytes(content)

    def rename(self, text, **fields):
        return ball_api.native_request(
            {"action": "rename-ball", "ball": str(self.ball), "text": text, **fields}
        )

    def assert_preserved(self, path):
        for name, content in self.protected.items():
            self.assertEqual((path / name).read_bytes(), content, name)
        self.assertFalse(list(path.glob(".2ine-export-*")))

    def test_stem_and_extension_rename_preserve_guest_and_manifest_bytes(self):
        original = (self.ball / "ball.json").read_bytes()
        result = self.rename("New café")
        destination = self.root / "New café.2ineball"
        self.assertEqual(result["path"], str(destination))
        self.assertEqual(
            result["snapshot"]["configSys"], self.protected["drive_c/CONFIG.SYS"].decode("latin1")
        )
        self.assertFalse(self.ball.exists())
        self.assertEqual((destination / "ball.json").read_bytes(), original)
        self.assert_preserved(destination)
        self.ball = destination
        result = self.rename("Final.2ineball")
        self.assertEqual(result["path"], str(self.root / "Final.2ineball"))
        self.assert_preserved(Path(result["path"]))

    def test_absolute_owned_paths_rebase_but_external_and_opaque_paths_do_not(self):
        image = self.ball / "media/floppy.img"
        image.parent.mkdir()
        ball_api.native_request(
            {
                "action": "command",
                "ball": str(self.ball),
                "arguments": ["create-floppy", "A", str(image), "1440"],
            }
        )
        owned = self.ball / "drive_c/Files"
        owned.mkdir()
        runtime = self.ball / "CustomRuntime"
        runtime.mkdir()
        sibling = self.root / (self.ball.name + "-external")
        sibling.mkdir()
        data = ball_api.load(self.ball)
        data["drives"].update(C=str(self.ball / "drive_c"), D=str(owned), E=str(sibling))
        data["runtime"]["build"] = str(runtime)
        data["soundBanks"] = [{"id": "owned", "file": str(self.ball / "soundfonts/owned.sf2")}]
        data["future"] = {"unchanged": str(self.ball), "integer": 18446744073709551615}
        data["environment"] = {
            "USER_INI": str(self.ball / "drive_c/OS2/OS2.INI"),
            "SYSTEM_INI": "C:\\OS2\\OS2SYS.INI",
            "ARBITRARY": str(self.ball),
        }
        data["applications"]["Guest"] = {"program": "C:\\OS2\\CMD.EXE", "cwd": "C:\\Files"}
        ball_api.save(self.ball / "ball.json", data)
        result = self.rename("New")
        destination = Path(result["path"])
        saved = ball_api.load(destination)
        self.assertEqual(saved["drives"]["C"], str(destination / "drive_c"))
        self.assertEqual(saved["drives"]["D"], str(destination / "drive_c/Files"))
        self.assertEqual(saved["drives"]["A"]["image"], str(destination / "media/floppy.img"))
        self.assertEqual(saved["drives"]["E"], str(sibling))
        self.assertEqual(saved["runtime"]["build"], str(destination / "CustomRuntime"))
        self.assertEqual(saved["soundBanks"][0]["file"], "soundfonts/owned.sf2")
        self.assertEqual(saved["future"], data["future"])
        self.assertEqual(saved["environment"]["USER_INI"], str(destination / "drive_c/OS2/OS2.INI"))
        self.assertEqual(saved["environment"]["SYSTEM_INI"], "C:\\OS2\\OS2SYS.INI")
        self.assertEqual(saved["environment"]["ARBITRARY"], str(self.ball))
        self.assertEqual(saved["applications"], data["applications"])
        self.assert_preserved(destination)
        ball_api.native_request({"action": "write-config", "ball": str(destination)})
        generated = (destination / "2ine.cfg").read_text()
        self.assertIn(str(destination / "drive_c"), generated)
        self.assertNotIn(str(self.ball) + "/", generated)

    def test_collision_never_replaces_existing_package_file_or_link(self):
        original = (self.ball / "ball.json").read_bytes()
        destination = self.root / "Existing.2ineball"
        destination.mkdir()
        marker = destination / "keep"
        marker.write_bytes(b"keep")
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.rename("Existing")
        self.assertEqual(marker.read_bytes(), b"keep")
        marker.unlink()
        destination.rmdir()
        destination.write_bytes(b"host file")
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.rename("Existing")
        self.assertEqual(destination.read_bytes(), b"host file")
        self.assertEqual((self.ball / "ball.json").read_bytes(), original)
        self.assert_preserved(self.ball)

    def test_unsafe_names_and_stale_manifest_are_non_destructive(self):
        for name in (
            "",
            ".",
            "..",
            "../escape",
            "other/name",
            "other\\name",
            "line\nfeed",
            "NUL",
            ".2ineball",
            "trailing.",
        ):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.rename(name)
        original = (self.ball / "ball.json").read_bytes()
        expected = hashlib.sha256(original).hexdigest()
        changed = original + b" \n"
        (self.ball / "ball.json").write_bytes(changed)
        with self.assertRaisesRegex(ValueError, "changed on disk"):
            self.rename("New", expected=expected)
        self.assertEqual((self.ball / "ball.json").read_bytes(), changed)
        self.assertFalse((self.root / "New.2ineball").exists())
        result = self.rename("New", expected=hashlib.sha256(changed).hexdigest())
        self.assert_preserved(Path(result["path"]))

    def test_source_must_be_a_package_even_when_it_contains_ball_json(self):
        plain = self.root / "ordinary folder"
        self.ball.rename(plain)
        with self.assertRaisesRegex(ValueError, "Only .2ineball"):
            ball_api.native_request({"action": "rename-ball", "ball": str(plain), "text": "New"})
        self.assert_preserved(plain)

    @unittest.skipIf(os.name == "nt", "requires POSIX symbolic links")
    def test_package_and_manifest_symlinks_are_rejected(self):
        alias = self.root / "Alias.2ineball"
        alias.symlink_to(self.ball, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "symbolic link"):
            ball_api.native_request({"action": "rename-ball", "ball": str(alias), "text": "New"})
        manifest = self.ball / "ball.json"
        actual = self.root / "manifest.json"
        manifest.rename(actual)
        manifest.symlink_to(actual)
        original = actual.read_bytes()
        with self.assertRaisesRegex(ValueError, "symbolic link"):
            self.rename("New")
        self.assertEqual(actual.read_bytes(), original)

    @unittest.skipIf(os.name == "nt", "requires POSIX symbolic links")
    def test_post_move_failure_rolls_back_manifest_and_package_without_overwrite(self):
        drive = self.ball / "drive_c"
        target = self.ball / "real_c"
        drive.rename(target)
        drive.symlink_to(target, target_is_directory=True)
        data = ball_api.load(self.ball)
        runtime = self.ball / "Runtime"
        runtime.mkdir()
        data["runtime"]["build"] = str(runtime)
        ball_api.save(self.ball / "ball.json", data)
        original = (self.ball / "ball.json").read_bytes()
        # The absolute internal link becomes inaccessible after the package move.
        with self.assertRaises(ValueError):
            self.rename("New")
        self.assertTrue(self.ball.is_dir())
        self.assertFalse((self.root / "New.2ineball").exists())
        self.assertEqual((self.ball / "ball.json").read_bytes(), original)
        self.assert_preserved(self.ball)


if __name__ == "__main__":
    unittest.main()
