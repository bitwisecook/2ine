# Public domain. No rights reserved. Wholly AI-authored.
"""Persistent bottle configuration, mapped paths and launcher isolation."""

import contextlib
import io
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import bottles


class BottleTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="2ine-bottles-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bottle = self.root / "one"
        bottles.init(self.bottle)
        self.data = bottles.load(self.bottle)

    def test_persistence_and_distinct_profiles(self):
        second = self.root / "two"
        bottles.init(second)
        first = bottles.environment(self.bottle, self.data, self.root, "unicorn")
        other = bottles.environment(second, bottles.load(second), self.root, "unicorn")
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
        bottles.init(self.bottle)
        self.assertEqual(
            Path(first["TWOINE_USER_PROFILE"]).read_bytes(), b"persistent application settings"
        )
        self.assertFalse(Path(other["TWOINE_USER_PROFILE"]).exists())

    def test_media_mapping_and_case_resolution(self):
        media = self.root / "media"
        (media / "ENGLISH").mkdir(parents=True)
        program = media / "ENGLISH/INSTALL.EXE"
        program.write_bytes(b"fixture")
        self.data["drives"]["D"] = str(media)
        bottles.write_config(self.bottle, self.data)
        self.assertEqual(
            bottles.host_path(r"D:\english\install.exe", self.bottle, self.data), program
        )
        self.assertEqual(
            bottles.guest_path(program, self.bottle, self.data), r"D:\ENGLISH\INSTALL.EXE"
        )
        config = (self.bottle / "2ine.cfg").read_text()
        self.assertIn(f"mountpoint.D = {media}\n", config)
        self.assertIn("mountpoint.Z = \n", config)
        self.assertNotIn("mountpoint.C = /\n", config)
        with self.assertRaises(ValueError):
            bottles.host_path(r"Z:\missing.exe", self.bottle, self.data)
        with self.assertRaises(ValueError):
            bottles.host_path(r"C:\..\..\outside", self.bottle, self.data)

    def test_child_launch_uses_guest_paths_and_private_settings(self):
        executable = self.bottle / "drive_c/OS2/app.exe"
        executable.write_bytes(b"fixture")
        build = self.root / "build"
        build.mkdir()
        (build / "lx_loader").write_bytes(b"fixture")
        args = [
            "--bottle",
            str(self.bottle),
            "--build",
            str(build),
            "install",
            r"C:\OS2\APP.EXE",
            "space argument",
            "",
        ]
        with patch.object(bottles.subprocess, "run") as run:
            run.return_value.returncode = 7
            with patch.dict(os.environ, {"TWOINE_REGISTRY": "/other/bottle", "IS_2INE": "old.exe"}):
                self.assertEqual(bottles.main(args), 7)
            call = run.call_args
            runtime = Path(call.kwargs["env"]["TWOINE_LIBPATH"].split(os.pathsep)[0])
            self.assertEqual(
                call.args[0], [str(runtime / "lx_loader"), r"C:\OS2\app.exe", "space argument", ""]
            )
            self.assertEqual(call.kwargs["cwd"], executable.parent)
            env = call.kwargs["env"]
            self.assertEqual(env["TWOINE_REGISTRY"], str(self.bottle / "drive_c/OS2/REGISTRY.INI"))
            self.assertNotIn("IS_2INE", env)
        receipt = json.loads((self.bottle / "logs/last-install.json").read_text())
        self.assertEqual(receipt["exit_code"], 7)

    def test_invalid_config_preserves_manifest(self):
        path = self.bottle / "bottle.json"
        before = path.read_bytes()
        with contextlib.redirect_stderr(io.StringIO()):
            result = bottles.main(
                ["--bottle", str(self.bottle), "config", "env.HOME", "/different"]
            )
        self.assertEqual(result, 2)
        self.assertEqual(before, path.read_bytes())
        with self.assertRaises(ValueError):
            bottles.init(self.root)

    def test_installed_config_and_profile_locations_are_reloaded(self):
        config = self.bottle / "drive_c/CONFIG.SYS"
        config.write_text(
            "SET PATH=C:\\LOTUS;C:\\OS2\nLIBPATH=C:\\LOTUS\\DLL\n"
            "SET USER_INI=C:\\OS2\\CUSTOM.INI\nSET LOTUS_HOME=C:\\LOTUS\n"
        )
        env = bottles.environment(self.bottle, self.data, self.root, "unicorn")
        self.assertEqual(env["OS2PATH"], r"C:\LOTUS;C:\OS2")
        self.assertEqual(env["LOTUS_HOME"], r"C:\LOTUS")
        self.assertEqual(env["TWOINE_USER_PROFILE"], str(self.bottle / "drive_c/OS2/CUSTOM.INI"))
        self.assertIn(str(self.bottle / "drive_c/LOTUS/DLL"), env["TWOINE_LIBPATH"])
        self.data["environment"]["OS2PATH"] = r"C:\OVERRIDE"
        self.assertEqual(
            bottles.environment(self.bottle, self.data, self.root, "unicorn")["OS2PATH"],
            r"C:\OVERRIDE",
        )

    def test_saved_application_preserves_arguments(self):
        executable = self.bottle / "drive_c/OS2/app.exe"
        executable.write_bytes(b"fixture")
        base = ["--bottle", str(self.bottle)]
        self.assertEqual(bottles.main([*base, "add-app", "Office", r"C:\OS2\APP.EXE", "a b"]), 0)
        app = bottles.load(self.bottle)["applications"]["Office"]
        self.assertEqual(app, {"program": r"C:\OS2\app.exe", "arguments": ["a b"]})
        with patch.object(bottles, "run", return_value=0) as run:
            self.assertEqual(bottles.main([*base, "launch", "Office", "document.lwp"]), 0)
            args = run.call_args.args[0]
            self.assertEqual(args.arguments, ["a b", "document.lwp"])

    def test_runtime_snapshots_keep_running_versions_intact(self):
        build = self.root / "build"
        build.mkdir()
        source = build / "libprobe.so"
        source.write_bytes(b"first runtime")
        loader = build / "lx_loader"
        loader.write_bytes(b"first loader")
        loader.chmod(0o755)
        first = bottles.environment(self.bottle, self.data, build, "unicorn")
        old = Path(first["TWOINE_LIBPATH"].split(os.pathsep)[0])
        bottles.sync_runtime(build, old)
        self.assertEqual((old / source.name).read_bytes(), b"first runtime")
        self.assertTrue(os.access(old / "lx_loader", os.X_OK))
        loader.write_bytes(b"second loader")
        changed_loader = bottles.environment(self.bottle, self.data, build, "unicorn")
        self.assertNotEqual(first["TWOINE_RUNTIME_ID"], changed_loader["TWOINE_RUNTIME_ID"])
        source.write_bytes(b"second runtime")
        second = bottles.environment(self.bottle, self.data, build, "unicorn")
        new = Path(second["TWOINE_LIBPATH"].split(os.pathsep)[0])
        self.assertNotEqual(old, new)
        self.assertNotEqual(first["TWOINE_RUNTIME_ID"], second["TWOINE_RUNTIME_ID"])
        bottles.sync_runtime(build, new)
        self.assertEqual((old / source.name).read_bytes(), b"first runtime")
        self.assertEqual((new / source.name).read_bytes(), b"second runtime")
        self.assertEqual((old / "lx_loader").read_bytes(), b"first loader")
        self.assertEqual((new / "lx_loader").read_bytes(), b"second loader")


if __name__ == "__main__":
    unittest.main()
