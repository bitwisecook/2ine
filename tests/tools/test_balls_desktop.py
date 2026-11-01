# Public domain. No rights reserved. Wholly AI-authored.
"""The desktop boundary must preserve shared manifests and installer edits."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools import balls, balls_desktop


class DesktopTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="2ine-desktop-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.ball = self.root / "Ball with spaces"
        balls.init(self.ball)

    def request(self, action, **fields):
        return balls_desktop.request({"action": action, "ball": str(self.ball), **fields})

    def test_config_sys_conflict_and_latin1_roundtrip(self):
        snapshot = self.request("snapshot")
        text = snapshot["configSys"] + "REM café\r\n"
        self.request("save-config-sys", expected=snapshot["configSys"], text=text)
        self.assertEqual(self.request("snapshot")["configSys"], text)
        path = self.ball / "drive_c/CONFIG.SYS"
        installer = path.read_bytes() + b"SET INSTALLER=changed\r\n"
        path.write_bytes(installer)
        with self.assertRaisesRegex(ValueError, "changed on disk"):
            self.request("save-config-sys", expected=text, text="discard installer")
        self.assertEqual(path.read_bytes(), installer)
        with self.assertRaisesRegex(ValueError, "Latin-1"):
            self.request("save-config-sys", expected=installer.decode("latin1"), text="😊")
        self.assertEqual(path.read_bytes(), installer)

    def test_saved_launcher_cwd_and_unknown_state_survive(self):
        data = balls.load(self.ball)
        data["future-field"] = {"keep": [1, 2, 3]}
        data["runtime"]["future-runtime"] = "keep"
        balls.save(self.ball / "ball.json", data)
        program = self.ball / "drive_c/OS2/app.exe"
        program.write_bytes(b"fixture")
        profile = self.ball / "drive_c/OS2/OS2.INI"
        profile.write_bytes(b"persistent")
        self.request(
            "command",
            arguments=[
                "add-app",
                "--cwd",
                "C:\\TEMP",
                "App",
                "C:\\OS2\\app.exe",
                "",
                "a b",
                "C:\\foo\\bar",
            ],
        )
        saved = balls.load(self.ball)
        self.assertEqual(saved["future-field"], data["future-field"])
        self.assertEqual(saved["runtime"], data["runtime"])
        self.assertEqual(saved["applications"]["App"]["cwd"], "C:\\TEMP")
        build = self.root / "build"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"fixture")
        plan = self.request("prepare-launch", build=str(build), arguments=["launch", "App"])
        self.assertEqual(Path(plan["cwd"]), (self.ball / "drive_c/TEMP").resolve())
        self.assertEqual(plan["command"][-3:], ["", "a b", "C:\\foo\\bar"])
        self.request("command", arguments=["remove-app", "App"])
        self.assertEqual(profile.read_bytes(), b"persistent")
        self.assertTrue(program.exists())

    def test_json_boundary_works_with_isolated_python(self):
        script = Path(balls_desktop.__file__)
        message = {"action": "snapshot", "ball": str(self.ball)}
        result = subprocess.run(
            [sys.executable, "-I", str(script)],
            input=json.dumps(message),
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertEqual(json.loads(result.stdout)["manifest"]["drives"]["C"], "drive_c")

    def test_runtime_selection_preserves_backend_and_rejects_missing_loader(self):
        build = self.root / "build"
        build.mkdir()
        with self.assertRaisesRegex(ValueError, "lx_loader"):
            self.request("runtime-build", build=str(build))
        (build / "lx_loader").write_bytes(b"runtime")
        self.request("command", arguments=["config", "runtime.backend", "unicorn"])
        snapshot = self.request("runtime-build", build=str(build))
        self.assertEqual(
            snapshot["manifest"]["runtime"], {"backend": "unicorn", "build": str(build.resolve())}
        )
