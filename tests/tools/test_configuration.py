# Public domain. No rights reserved. Wholly AI-authored.
"""Binary profile editing, revision guards and per-ball display settings."""

import hashlib
import struct
import tempfile
import unittest
from pathlib import Path

from tools.ball import engine as ball_api


def profile(entries):
    wire = bytearray(b"2INEPRF1" + struct.pack("<I", len(entries)))
    for app, key, data in entries:
        app, key = app.encode("latin1") + b"\0", key.encode("latin1") + b"\0"
        wire.extend(struct.pack("<III", len(app), len(key), len(data)))
        wire.extend(app + key + data)
    return bytes(wire)


class ConfigurationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="2ine-config-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.ball = self.root / "Fixture.2ineball"
        ball_api.init(self.ball)

    def request(self, action="configuration-read", **fields):
        return ball_api.native_request({"action": action, "ball": str(self.ball), **fields})

    def test_binary_values_survive_text_edit_and_conflict_is_non_destructive(self):
        path = self.ball / "drive_c/OS2/OS2.INI"
        source = profile([("App", "Binary", bytes(range(256))), ("App", "Text", b"caf\xe9\0")])
        path.write_bytes(source)
        current = self.request(name="user")
        self.assertEqual(current["revision"], hashlib.sha256(source).hexdigest())
        saved = self.request(
            "configuration-write",
            name="user",
            expected=current["revision"],
            path="App",
            key="Text",
            text=b"new\0".hex(),
        )
        self.assertEqual(saved["entries"][0]["data"], bytes(range(256)).hex())
        unchanged = path.read_bytes()
        with self.assertRaisesRegex(ValueError, "changed on disk"):
            self.request(
                "configuration-write",
                name="user",
                expected=current["revision"],
                path="App",
                key="Binary",
                text="ff",
            )
        self.assertEqual(path.read_bytes(), unchanged)

    def test_native_os2_profile_is_read_and_preserves_other_bytes_when_saved(self):
        # Native header/app/key links follow the runtime's documented INI reader.
        source = bytearray(128)
        struct.pack_into("<III", source, 0, 0xFFFFFFFF, 20, len(source))
        struct.pack_into("<II", source, 20, 0, 40)
        struct.pack_into("<H", source, 32, 4)
        struct.pack_into("<I", source, 36, 80)
        struct.pack_into("<I", source, 40, 0)
        struct.pack_into("<H", source, 48, 4)
        struct.pack_into("<I", source, 52, 84)
        struct.pack_into("<H", source, 56, 3)
        struct.pack_into("<I", source, 60, 88)
        source[80:91] = b"App\0Key\0\x00\xff\x81"
        (self.ball / "drive_c/OS2/OS2.INI").write_bytes(source)
        current = self.request(name="user")
        self.assertEqual(current["format"], "OS/2 INI")
        saved = self.request(
            "configuration-write",
            name="user",
            expected=current["revision"],
            path="Other",
            key="New",
            text="01000000",
        )
        self.assertEqual(saved["format"], "2INEPRF1")
        self.assertEqual(
            saved["entries"][0], {"application": "App", "key": "Key", "data": "00ff81"}
        )

    def test_corrupt_profile_is_never_overwritten(self):
        path = self.ball / "drive_c/OS2/OS2SYS.INI"
        corrupt = b"2INEPRF1\x01\0\0\0\xff\xff\xff\xff"
        path.write_bytes(corrupt)
        with self.assertRaisesRegex(ValueError, "invalid or unsupported"):
            self.request(
                "configuration-write",
                name="system",
                expected="anything",
                path="App",
                key="Key",
                text="00",
            )
        self.assertEqual(path.read_bytes(), corrupt)

    def test_registry_type_prefix_key_marker_and_delete(self):
        current = self.request(name="registry")
        saved = self.request(
            "configuration-write",
            name="registry",
            expected=current["revision"],
            path="HKCU\\Software\\Fixture",
            key="V:Count",
            text="040000002a000000",
        )
        self.assertIn(
            {"application": "hkcu\\software\\fixture", "key": "k:", "data": "00"}, saved["entries"]
        )
        saved = self.request(
            "configuration-write",
            name="registry",
            expected=saved["revision"],
            path="hkcu\\software\\fixture",
            key="v:count",
            remove=True,
        )
        self.assertFalse(any(entry["key"] == "v:count" for entry in saved["entries"]))
        before = (self.ball / "drive_c/OS2/REGISTRY.INI").read_bytes()
        with self.assertRaisesRegex(ValueError, "typed v:"):
            self.request(
                "configuration-write",
                name="registry",
                expected=saved["revision"],
                path="hkcu\\software",
                key="k:",
                text="00",
            )
        self.assertEqual((self.ball / "drive_c/OS2/REGISTRY.INI").read_bytes(), before)

    def test_profile_paths_honor_config_sys_user_ini(self):
        path = self.ball / "drive_c/CONFIG.SYS"
        path.write_bytes(path.read_bytes() + b"SET USER_INI=C:\\OS2\\ALTERNATE.INI\r\n")
        self.assertEqual(
            self.request(name="user")["path"], str(self.ball / "drive_c/OS2/ALTERNATE.INI")
        )

    def test_window_chrome_choices_preserve_other_settings_and_reject_bad_values(self):
        self.request("command", arguments=["config", "system.display_dpi", "144"])
        for choice in ("native", "os2", "both"):
            result = self.request("command", arguments=["config", "system.window_chrome", choice])
            self.assertEqual(result["manifest"]["system"]["window_chrome"], choice)
            self.assertEqual(result["manifest"]["system"]["display_dpi"], "144")
            self.assertIn("system.window_chrome = " + choice, (self.ball / "2ine.cfg").read_text())
        before = (self.ball / "ball.json").read_bytes()
        with self.assertRaisesRegex(ValueError, "window_chrome"):
            self.request("command", arguments=["config", "system.window_chrome", "unknown"])
        self.assertEqual((self.ball / "ball.json").read_bytes(), before)
        self.request("command", arguments=["unset", "system.window_chrome"])
        self.assertNotIn("window_chrome", self.request("snapshot")["manifest"]["system"])

    def test_display_dpi_validation_and_launch_environment(self):
        self.request("command", arguments=["config", "system.display_dpi", "144"])
        build = self.root / "build"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"fixture")
        program = self.ball / "drive_c/program.exe"
        program.write_bytes(b"fixture")
        plan = self.request(
            "prepare-launch", build=str(build), arguments=["run", "C:\\program.exe"]
        )
        self.assertEqual(plan["environment"]["TWOINE_DISPLAY_DPI"], "144")
        for bad in ["0", "385", "96.5", "nan", "144x"]:
            with self.assertRaisesRegex(ValueError, "display_dpi"):
                self.request("command", arguments=["config", "system.display_dpi", bad])
        self.assertEqual(self.request("snapshot")["manifest"]["system"]["display_dpi"], "144")

    def test_display_depth_defaults_to_true_colour_and_validates(self):
        build = self.root / "build"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"fixture")
        program = self.ball / "drive_c/program.exe"
        program.write_bytes(b"fixture")
        arguments = ["run", "C:\\program.exe"]
        plan = self.request("prepare-launch", build=str(build), arguments=arguments)
        self.assertEqual(plan["environment"]["TWOINE_DISPLAY_DEPTH"], "24")
        self.request("command", arguments=["config", "system.display_depth", "8"])
        plan = self.request("prepare-launch", build=str(build), arguments=arguments)
        self.assertEqual(plan["environment"]["TWOINE_DISPLAY_DEPTH"], "8")
        for bad in ["0", "1", "32", "8x"]:
            with self.assertRaisesRegex(ValueError, "display_depth"):
                self.request("command", arguments=["config", "system.display_depth", bad])
        self.assertEqual(self.request("snapshot")["manifest"]["system"]["display_depth"], "8")


if __name__ == "__main__":
    unittest.main()
