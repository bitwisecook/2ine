# Public domain. No rights reserved. Wholly AI-authored.
"""Discover and edit only existing private C: INIs without losing binary values."""

import hashlib
import tempfile
import unittest
from pathlib import Path

from tests.ball.test_configuration import profile
from tools.ball import engine as ball_api


class ConfigurationFileTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="2ine-configuration-files-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.ball = self.root / "Test.2ineball"
        ball_api.init(self.ball)
        self.c = self.ball / "drive_c"

    def request(self, action="configuration-files", **fields):
        return ball_api.native_request({"action": action, "ball": str(self.ball), **fields})

    def installed(self, name, data):
        path = self.c / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def test_missing_standard_profiles_have_named_editor_descriptors(self):
        listing = self.request()
        profiles = {
            entry["role"]: entry
            for entry in listing["files"]
            if entry["role"] in ("user", "system")
        }
        self.assertEqual(set(profiles), {"user", "system"})
        for role, name in (("user", "OS2.INI"), ("system", "OS2SYS.INI")):
            row = profiles[role]
            self.assertEqual(row["path"].upper(), "C:\\OS2\\" + name)
            self.assertFalse(row["exists"])
            self.assertEqual(row["kind"], "profile")
            self.assertTrue(row["editable"])
            self.assertEqual(row["reason"], "Created on first save")
            with self.assertRaises(ValueError):
                self.request("configuration-file-read", path=row["path"])
            current = self.request("configuration-read", name=role)
            self.request(
                "configuration-write",
                name=role,
                expected=current["revision"],
                path="App",
                key="Value",
                text="00ff",
            )
            self.assertTrue((self.c / "OS2" / name).is_file())

    def test_text_roundtrip_revision_conflict_and_encoding_are_safe(self):
        original = b"; keep comments\r\n[App]\r\nName=caf\xe9\r\n"
        path = self.installed("APP/settings.ini", original)
        read = self.request("configuration-file-read", path=r"C:\app\SETTINGS.INI")
        self.assertEqual(read["kind"], "text")
        self.assertTrue(read["editable"])
        self.assertEqual(read["text"], original.decode("latin1"))
        self.assertEqual(read["revision"], hashlib.sha256(original).hexdigest())
        saved = self.request(
            "configuration-file-write",
            path=read["path"],
            expected=read["revision"],
            text=read["text"].replace("caf\xe9", "th\xe9"),
        )
        self.assertEqual(path.read_bytes(), original.replace(b"caf\xe9", b"th\xe9"))
        path.write_bytes(b"external change\r\n")
        with self.assertRaisesRegex(ValueError, "changed on disk"):
            self.request(
                "configuration-file-write",
                path=read["path"],
                expected=saved["revision"],
                text="draft",
            )
        self.assertEqual(path.read_bytes(), b"external change\r\n")
        current = self.request("configuration-file-read", path=read["path"])
        for text in ("emoji \U0001f600", "binary\x01control"):
            with self.assertRaises(ValueError):
                self.request(
                    "configuration-file-write",
                    path=read["path"],
                    expected=current["revision"],
                    text=text,
                )
        self.assertEqual(path.read_bytes(), b"external change\r\n")

    def test_profiles_use_explicit_application_and_preserve_binary_values(self):
        path = self.installed(
            "APP/PROFILE.INI",
            profile([("App", "Binary", bytes(range(256))), ("App", "Text", b"old\0")]),
        )
        current = self.request("configuration-file-read", path=r"C:\APP\PROFILE.INI")
        self.assertEqual(current["kind"], "profile")
        saved = self.request(
            "configuration-file-write",
            path=current["path"],
            expected=current["revision"],
            application="App",
            key="Text",
            text="6e657700",
        )
        self.assertEqual(saved["entries"][0]["data"], bytes(range(256)).hex())
        self.assertEqual(saved["entries"][1]["data"], "6e657700")
        saved = self.request(
            "configuration-file-write",
            path=current["path"],
            expected=saved["revision"],
            application="App",
            key="Other",
            data="ff0001",
        )
        self.assertEqual(saved["entries"][-1]["data"], "ff0001")
        self.assertTrue(path.read_bytes().startswith(b"2INEPRF1"))
        for name, data in [
            ("UNKNOWN.INI", b"\0\x01\xffopaque"),
            ("CORRUPT.INI", b"2INEPRF1\x01\0\0\0broken"),
        ]:
            unknown = self.installed(name, data)
            snapshot = self.request("configuration-file-read", path="C:\\" + name)
            self.assertEqual(snapshot["kind"], "binary")
            self.assertFalse(snapshot["editable"])
            self.assertEqual(snapshot["data"], data.hex())
            with self.assertRaisesRegex(ValueError, "read-only"):
                self.request(
                    "configuration-file-write",
                    path=snapshot["path"],
                    expected=snapshot["revision"],
                    text="replacement",
                )
            self.assertEqual(unknown.read_bytes(), data)

    def test_discovery_deduplicates_configured_profile_aliases_and_excludes_caches(self):
        self.installed("APP/CUSTOM.INI", b"")
        for key in ("USER_INI", "SYSTEM_INI"):
            self.request("command", arguments=["config", "env." + key, r"C:\APP\CUSTOM.INI"])
        self.installed("APP/TEXT.INI", b"[Section]\nName=value\n")
        self.installed("APP/TEMP/HIDDEN.INI", b"hidden")
        self.installed("OS2/DLL/2INE/cache/HIDDEN.INI", b"hidden")
        self.installed("TEMP/HIDDEN.INI", b"hidden")
        listing = self.request()
        paths = [entry["path"].upper() for entry in listing["files"]]
        self.assertIn(r"C:\CONFIG.SYS", paths)
        aliases = [
            entry for entry in listing["files"] if entry["path"].upper() == r"C:\APP\CUSTOM.INI"
        ]
        self.assertEqual(len(aliases), 1)
        self.assertEqual(aliases[0]["roles"], ["user", "system"])
        self.assertEqual(aliases[0]["kind"], "profile")
        self.assertFalse(any("HIDDEN.INI" in path for path in paths))
        self.assertGreaterEqual(listing["skipped"], 3)
        limited = self.request(limit=1)
        self.assertTrue(limited["truncated"])
        self.assertEqual(len(limited["files"]), 1)
        self.assertEqual(limited["limit"], 1)

    def test_paths_reject_links_escapes_external_drives_and_other_file_types(self):
        safe = self.installed("APP/SAFE.INI", b"safe")
        outside = self.root / "outside"
        outside.mkdir()
        foreign = outside / "OUTSIDE.INI"
        foreign.write_bytes(b"foreign")
        (self.c / "LINK.INI").symlink_to(foreign)
        (self.c / "LINKDIR").symlink_to(outside, target_is_directory=True)
        for name in [
            r"C:\LINK.INI",
            r"C:\LINKDIR\OUTSIDE.INI",
            r"C:\APP\..\APP\SAFE.INI",
            r"D:\OUTSIDE.INI",
            str(foreign),
            r"C:APP\SAFE.INI",
            r"C:\APP\SAFE.EXE",
            r"C:\TEMP\HIDDEN.INI",
        ]:
            with self.assertRaises(ValueError):
                self.request("configuration-file-read", path=name)
            with self.assertRaises(ValueError):
                self.request(
                    "configuration-file-write", path=name, expected="anything", text="changed"
                )
        self.assertEqual(foreign.read_bytes(), b"foreign")
        self.assertEqual(safe.read_bytes(), b"safe")
        listing = self.request()
        self.assertFalse(any("LINK" in entry["path"] for entry in listing["files"]))
        manifest = ball_api.load(self.ball)
        manifest["drives"]["C"] = str(outside)
        ball_api.save(self.ball / "ball.json", manifest)
        with self.assertRaisesRegex(ValueError, "private"):
            self.request(
                "configuration-file-write",
                path=r"C:\OUTSIDE.INI",
                expected=hashlib.sha256(b"foreign").hexdigest(),
                text="changed",
            )
        self.assertEqual(foreign.read_bytes(), b"foreign")


if __name__ == "__main__":
    unittest.main()
