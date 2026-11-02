# Public domain. No rights reserved. Wholly AI-authored.
"""Persistent ball configuration, mapped paths and launcher isolation."""

import contextlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import balls


class BallTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="2ine-balls-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.ball = self.root / "one"
        balls.init(self.ball)
        self.data = balls.load(self.ball)

    def test_reset_settings_and_remove_launcher_preserve_installation(self):
        self.data["environment"]["LANG"] = "de_DE"
        self.data["runtime"]["backend"] = "unicorn"
        self.data["applications"]["Editor"] = {"program": r"C:\OS2\EDITOR.EXE", "arguments": []}
        program = self.ball / "drive_c/OS2/EDITOR.EXE"
        program.write_bytes(b"installed")
        balls.save(self.ball / "ball.json", self.data)
        base = ["--ball", str(self.ball)]
        self.assertEqual(balls.main([*base, "unset", "env.LANG"]), 0)
        self.assertEqual(balls.main([*base, "unset", "runtime.backend"]), 0)
        self.assertEqual(balls.main([*base, "remove-app", "Editor"]), 0)
        data = balls.load(self.ball)
        self.assertEqual(data["environment"], {})
        self.assertEqual(data["runtime"], {})
        self.assertEqual(data["applications"], {})
        self.assertEqual(program.read_bytes(), b"installed")

    def test_persistence_and_distinct_profiles(self):
        second = self.root / "two"
        balls.init(second)
        first = balls.environment(self.ball, self.data, self.root, "unicorn")
        other = balls.environment(second, balls.load(second), self.root, "unicorn")
        for key in (
            "TWOINE_CONFIG",
            "TWOINE_USER_PROFILE",
            "TWOINE_SYSTEM_PROFILE",
            "TWOINE_REGISTRY",
            "TWOINE_OBJECTS",
            "TWOINE_IPC_DIR",
        ):
            self.assertNotEqual(first[key], other[key])
        Path(first["TWOINE_USER_PROFILE"]).write_bytes(b"persistent application settings")
        balls.init(self.ball)
        self.assertEqual(
            Path(first["TWOINE_USER_PROFILE"]).read_bytes(), b"persistent application settings"
        )
        self.assertFalse(Path(other["TWOINE_USER_PROFILE"]).exists())
        with patch.dict(os.environ, {"LANG": "C.UTF-8"}):
            self.assertEqual(
                balls.environment(self.ball, self.data, self.root, "unicorn")["LANG"], "en_US"
            )
        self.data["environment"]["LANG"] = "de_DE"
        self.assertEqual(
            balls.environment(self.ball, self.data, self.root, "unicorn")["LANG"], "de_DE"
        )

    def test_media_mapping_and_case_resolution(self):
        media = self.root / "media"
        (media / "ENGLISH").mkdir(parents=True)
        program = media / "ENGLISH/INSTALL.EXE"
        program.write_bytes(b"fixture")
        self.data["drives"]["D"] = str(media)
        balls.write_config(self.ball, self.data)
        self.assertEqual(balls.host_path(r"D:\english\install.exe", self.ball, self.data), program)
        self.assertEqual(balls.guest_path(program, self.ball, self.data), r"D:\ENGLISH\INSTALL.EXE")
        config = (self.ball / "2ine.cfg").read_text()
        self.assertIn(f"mountpoint.D = {media}\n", config)
        self.assertIn("mountpoint.Z = \n", config)
        self.assertNotIn("mountpoint.C = /\n", config)
        with self.assertRaises(ValueError):
            balls.host_path(r"Z:\missing.exe", self.ball, self.data)
        with self.assertRaises(ValueError):
            balls.host_path(r"C:\..\..\outside", self.ball, self.data)

    def test_child_launch_uses_guest_paths_and_private_settings(self):
        executable = self.ball / "drive_c/OS2/app.exe"
        executable.write_bytes(b"fixture")
        build = self.root / "build"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"fixture")
        args = [
            "--ball",
            str(self.ball),
            "--build",
            str(build),
            "install",
            r"C:\OS2\APP.EXE",
            "space argument",
            "",
        ]
        with patch.object(balls.subprocess, "run") as run:
            run.return_value.returncode = 7
            with patch.dict(os.environ, {"TWOINE_REGISTRY": "/other/ball", "IS_2INE": "old.exe"}):
                self.assertEqual(balls.main(args), 7)
            call = run.call_args
            runtime = Path(call.kwargs["env"]["TWOINE_LIBPATH"].split(os.pathsep)[0])
            self.assertEqual(
                call.args[0], [str(runtime / "lx_loader"), r"C:\OS2\app.exe", "space argument", ""]
            )
            self.assertEqual(call.kwargs["cwd"], executable.parent)
            env = call.kwargs["env"]
            self.assertEqual(env["TWOINE_REGISTRY"], str(self.ball / "drive_c/OS2/REGISTRY.INI"))
            self.assertNotIn("IS_2INE", env)
        receipt = json.loads((self.ball / "logs/last-install.json").read_text())
        self.assertEqual(receipt["exit_code"], 7)

    def test_invalid_config_preserves_manifest(self):
        path = self.ball / "ball.json"
        before = path.read_bytes()
        with contextlib.redirect_stderr(io.StringIO()):
            result = balls.main(["--ball", str(self.ball), "config", "env.HOME", "/different"])
        self.assertEqual(result, 2)
        self.assertEqual(before, path.read_bytes())
        with self.assertRaises(ValueError):
            balls.init(self.root)

    def test_installed_config_and_profile_locations_are_reloaded(self):
        config = self.ball / "drive_c/CONFIG.SYS"
        config.write_text(
            "SET PATH=C:\\LOTUS;C:\\OS2\nLIBPATH=C:\\LOTUS\\DLL\n"
            "SET USER_INI=C:\\OS2\\CUSTOM.INI\nSET LOTUS_HOME=C:\\LOTUS\n"
        )
        env = balls.environment(self.ball, self.data, self.root, "unicorn")
        self.assertEqual(env["OS2PATH"], r"C:\LOTUS;C:\OS2")
        self.assertEqual(env["LOTUS_HOME"], r"C:\LOTUS")
        self.assertEqual(env["TWOINE_USER_PROFILE"], str(self.ball / "drive_c/OS2/CUSTOM.INI"))
        self.assertIn(str(self.ball / "drive_c/LOTUS/DLL"), env["TWOINE_LIBPATH"])
        self.data["environment"]["OS2PATH"] = r"C:\OVERRIDE"
        self.assertEqual(
            balls.environment(self.ball, self.data, self.root, "unicorn")["OS2PATH"],
            r"C:\OVERRIDE",
        )

    def test_saved_application_preserves_arguments(self):
        executable = self.ball / "drive_c/OS2/app.exe"
        executable.write_bytes(b"fixture")
        build = self.root / "build"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"fixture")
        base = ["--ball", str(self.ball), "--build", str(build)]
        self.assertEqual(balls.main([*base, "add-app", "Office", r"C:\OS2\APP.EXE", "a b"]), 0)
        app = balls.load(self.ball)["applications"]["Office"]
        self.assertEqual(app, {"program": r"C:\OS2\app.exe", "arguments": ["a b"]})
        with patch.object(balls.subprocess, "run") as run:
            run.return_value.returncode = 0
            self.assertEqual(balls.main([*base, "launch", "Office", "document.lwp"]), 0)
            self.assertEqual(run.call_args.args[0][-2:], ["a b", "document.lwp"])

    def test_runtime_snapshots_keep_running_versions_intact(self):
        build = self.root / "build"
        build.mkdir()
        source = build / "libprobe.so"
        source.write_bytes(b"first runtime")
        loader = build / "lx_loader"
        loader.write_bytes(b"first loader")
        loader.chmod(0o755)
        first = balls.environment(self.ball, self.data, build, "unicorn")
        old = Path(first["TWOINE_LIBPATH"].split(os.pathsep)[0])
        balls.sync_runtime(build, old)
        self.assertEqual((old / source.name).read_bytes(), b"first runtime")
        self.assertTrue(os.access(old / "lx_loader", os.X_OK))
        loader.write_bytes(b"second loader")
        changed_loader = balls.environment(self.ball, self.data, build, "unicorn")
        self.assertNotEqual(first["TWOINE_RUNTIME_ID"], changed_loader["TWOINE_RUNTIME_ID"])
        source.write_bytes(b"second runtime")
        second = balls.environment(self.ball, self.data, build, "unicorn")
        new = Path(second["TWOINE_LIBPATH"].split(os.pathsep)[0])
        self.assertNotEqual(old, new)
        self.assertNotEqual(first["TWOINE_RUNTIME_ID"], second["TWOINE_RUNTIME_ID"])
        balls.sync_runtime(build, new)
        self.assertEqual((old / source.name).read_bytes(), b"first runtime")
        self.assertEqual((new / source.name).read_bytes(), b"second runtime")
        self.assertEqual((old / "lx_loader").read_bytes(), b"first loader")
        self.assertEqual((new / "lx_loader").read_bytes(), b"second loader")


if __name__ == "__main__":
    unittest.main()
