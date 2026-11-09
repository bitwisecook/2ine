# Public domain. No rights reserved. Wholly AI-authored.
"""Mach-O identity records must not enter the relocated dependency closure."""

import subprocess
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.macos import macos_bundle


class DependencyTests(unittest.TestCase):
    def test_dylib_identity_is_not_a_load_dependency(self):
        identity = "@rpath/libSDL3.0.dylib"
        linked = "/usr/lib/libSystem.B.dylib"
        outputs = [
            f"SDL:\n\t{identity} (compatibility version 401.0.0, current version 401.18.0)\n\t{linked} (compatibility version 1.0.0, current version 1.0.0)\n",
            f"SDL:\n{identity}\n",
        ]
        with patch.object(
            macos_bundle,
            "run",
            side_effect=[subprocess.CompletedProcess([], 0, stdout=output) for output in outputs],
        ):
            self.assertEqual(macos_bundle.dependencies(Path("SDL")), [linked])

    def test_executable_keeps_its_first_real_dependency(self):
        linked = "@rpath/lib2ine.dylib"
        outputs = [
            f"loader:\n\t{linked} (compatibility version 0.0.0, current version 0.0.0)\n",
            "loader:\n",
        ]
        with patch.object(
            macos_bundle,
            "run",
            side_effect=[subprocess.CompletedProcess([], 0, stdout=output) for output in outputs],
        ):
            self.assertEqual(macos_bundle.dependencies(Path("loader")), [linked])
