# Public domain. No rights reserved. Wholly AI-authored.
"""Portable per-ball graphics and media choices resolve against the receiving runtime."""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.ball import engine as ball_api


class RuntimeFeatureTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="2ine-runtime-features-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.ball = self.root / "Test.2ineball"
        ball_api.init(self.ball)
        self.build = self.root / "Receiving runtime"
        self.build.mkdir()
        (self.build / "lx_loader").write_bytes(b"fixture")
        (self.ball / "drive_c/TEST.EXE").write_bytes(b"fixture")
        self.capabilities(mesa=False, video=True, pcm=True, midi=True, dive=True)

    def capabilities(self, **features):
        (self.build / "runtime-capabilities.json").write_text(
            json.dumps(
                {
                    "schema": 1,
                    "backends": ["unicorn"],
                    "defaultBackend": "unicorn",
                    "features": features,
                }
            )
        )

    def request(self, action="runtime-features", **fields):
        return ball_api.native_request(
            {"action": action, "ball": str(self.ball), "build": str(self.build), **fields}
        )

    def configure(self, key, value):
        return self.request("command", arguments=["config", "system." + key, value])

    def test_defaults_and_effective_choices_preserve_requested_bytes(self):
        defaults = self.request()
        self.assertEqual(defaults["opengl"]["default"], "mesa")
        self.assertIsNone(defaults["opengl"]["requested"])
        self.assertFalse(defaults["opengl"]["available"])
        self.assertEqual(defaults["opengl"]["effective"], "off")
        for key in ("video", "pcm", "midi", "dive"):
            self.assertIsNone(defaults[key]["requested"])
            self.assertTrue(defaults[key]["effective"])
        self.configure("pcm", "false")
        self.configure("midi", "true")
        self.configure("video", "true")
        before = (self.ball / "ball.json").read_bytes()
        self.capabilities(mesa=False, video=False, pcm=True, midi=True, dive=True)
        state = self.request()
        self.assertFalse(state["pcm"]["effective"])
        self.assertTrue(state["midi"]["effective"])
        self.assertTrue(state["video"]["requested"])
        self.assertFalse(state["video"]["available"])
        self.assertFalse(state["video"]["effective"])
        with self.assertRaisesRegex(ValueError, "Selected video feature is unavailable"):
            self.request("prepare-launch", arguments=["run", "C:\\TEST.EXE"])
        self.assertEqual((self.ball / "ball.json").read_bytes(), before)
        self.configure("video", "false")
        self.request("prepare-launch", arguments=["run", "C:\\TEST.EXE"])

    def test_original_provider_can_be_staged_then_become_available(self):
        self.configure("opengl", "original")
        state = self.request()["opengl"]
        self.assertEqual(state["requested"], "original")
        self.assertFalse(state["providers"]["original"]["available"])
        with self.assertRaisesRegex(ValueError, "original OS/2 OPENGL.DLL"):
            self.request("prepare-launch", arguments=["run", "C:\\TEST.EXE"])
        (self.ball / "drive_c/OS2/DLL/OPENGL.DLL").write_bytes(b"user-supplied original")
        before = (self.ball / "ball.json").read_bytes()
        state = self.request()["opengl"]
        self.assertTrue(state["available"])
        self.assertFalse(state["providers"]["original"]["qualified"])
        self.assertEqual(state["effective"], "original")
        self.assertEqual((self.ball / "ball.json").read_bytes(), before)
        self.configure("opengl", "mesa")
        self.assertFalse(self.request()["opengl"]["available"])
        self.capabilities(mesa=True, video=True, pcm=True, midi=True, dive=True)
        self.assertEqual(self.request()["opengl"]["effective"], "mesa")
        self.configure("opengl", "off")
        self.assertEqual(self.request()["opengl"]["effective"], "off")

    def test_legacy_or_missing_mesa_metadata_never_defaults_to_installed_original(self):
        (self.ball / "drive_c/OS2/DLL/OPENGL.DLL").write_bytes(b"user-supplied original")
        metadata = self.build / "runtime-capabilities.json"
        before = (self.ball / "ball.json").read_bytes()
        for declaration in (None, {"schema": 1}, {"schema": 1, "features": {"mesa": False}}):
            if declaration is None:
                metadata.unlink(missing_ok=True)
            else:
                metadata.write_text(
                    json.dumps(
                        {"backends": ["unicorn"], "defaultBackend": "unicorn", **declaration}
                    )
                )
            state = self.request()["opengl"]
            self.assertIsNone(state["requested"])
            self.assertEqual(state["default"], "mesa")
            self.assertFalse(state["available"])
            self.assertEqual(state["effective"], "off")
            self.assertTrue(state["providers"]["original"]["available"])
            self.assertIn("Mesa OpenGL support", state["reason"])
            # An unused, absent default is not a reason to block other programs.
            self.request("prepare-launch", arguments=["run", "C:\\TEST.EXE"])
            self.assertEqual((self.ball / "ball.json").read_bytes(), before)

    def test_validation_and_cache_environment_preserve_host_tail(self):
        for key, value in (
            ("opengl", "native"),
            ("video", "maybe"),
            ("pcm", "2"),
            ("midi", ""),
            ("dive", "yes please"),
        ):
            with self.assertRaises(ValueError):
                self.configure(key, value)
        with self.assertRaisesRegex(ValueError, "Reserved host"):
            self.request("command", arguments=["config", "env.DYLD_LIBRARY_PATH", "guest override"])
        with patch.dict(os.environ, {"DYLD_LIBRARY_PATH": "/host/library/tail"}):
            plan = self.request("prepare-launch", arguments=["run", "C:\\TEST.EXE"])
        self.assertIn("DYLD_LIBRARY_PATH", plan["unsetEnvironment"])
        if sys.platform == "darwin":
            paths = plan["environment"]["DYLD_LIBRARY_PATH"].split(os.pathsep)
            self.assertEqual(Path(paths[0]), Path(plan["command"][0]).parent)
            self.assertEqual(paths[-1], "/host/library/tail")
        else:
            self.assertNotIn("DYLD_LIBRARY_PATH", plan["environment"])


if __name__ == "__main__":
    unittest.main()
