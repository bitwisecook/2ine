# Public domain. No rights reserved. Wholly AI-authored.
"""Verify bundle build reservations survive rebuilds, failures and concurrent processes."""

import concurrent.futures
import os
import plistlib
import tempfile
import unittest
from pathlib import Path

from tools.macos import macos_build_number


@unittest.skipUnless(os.name == "posix", "macOS build counters use POSIX file locks")
class BundleBuildNumberTests(unittest.TestCase):
    def test_rebuild_uses_persistent_counter_and_existing_bundle_floor(self):
        with tempfile.TemporaryDirectory() as directory:
            counter = Path(directory) / "state/build-number"
            self.assertEqual(macos_build_number.next_build_number(counter), 2)
            self.assertEqual(macos_build_number.next_build_number(counter), 3)
            self.assertEqual(macos_build_number.next_build_number(counter, minimum=40), 41)
            self.assertEqual(macos_build_number.next_build_number(counter), 42)

    def test_processes_reserve_distinct_increasing_builds(self):
        with tempfile.TemporaryDirectory() as directory:
            counter = Path(directory) / "build-number"
            with concurrent.futures.ProcessPoolExecutor(max_workers=4) as workers:
                numbers = list(workers.map(macos_build_number.next_build_number, [counter] * 16))
            self.assertEqual(sorted(numbers), list(range(2, 18)))
            self.assertEqual(macos_build_number.next_build_number(counter), 18)

    def test_invalid_state_fails_instead_of_reusing_builds(self):
        with tempfile.TemporaryDirectory() as directory:
            counter = Path(directory) / "build-number"
            counter.write_text("damaged\n")
            with self.assertRaisesRegex(ValueError, "refusing to reset"):
                macos_build_number.next_build_number(counter)
            self.assertEqual(counter.read_text(), "damaged\n")

    def test_generated_plist_preserves_version_and_advances_after_output_clean(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            template, output, counter = (root / name for name in ("template", "output", "counter"))
            template.write_bytes(
                plistlib.dumps({"CFBundleVersion": "1", "CFBundleShortVersionString": "0.1"})
            )
            first = macos_build_number.stamp(output, template, counter)
            output.unlink()
            second = macos_build_number.stamp(output, template, counter)
            self.assertEqual(second, first + 1)
            info = plistlib.loads(output.read_bytes())
            self.assertEqual(info["CFBundleVersion"], str(second))
            self.assertEqual(info["CFBundleShortVersionString"], "0.1")
            self.assertEqual(plistlib.loads(template.read_bytes())["CFBundleVersion"], "1")
            self.assertEqual(output.stat().st_mode & 0o777, 0o644)


if __name__ == "__main__":
    unittest.main()
