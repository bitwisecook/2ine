# Public domain. No rights reserved. Wholly AI-authored.
"""Package-only management validation and independent GTK ball references."""

import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

from apps.gnome.twoine_gnome import ball_lifecycle as lifecycle


class BallLifecycleTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="2ine-gtk-lifecycle-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.source = self.make_ball("Office")
        self.other = self.make_ball("Other")

    def make_ball(self, name):
        path = self.root / (name + ".2ineball")
        path.mkdir()
        (path / "ball.json").write_text(json.dumps({"version": 1, "drives": {"C": "drive_c"}}))
        return path

    def window(self, selected=None):
        a, b = self.source, self.other
        return SimpleNamespace(
            external=[a, b],
            ball=selected or a,
            snapshot={"ball": str(a)},
            drafts={a: ("base", "draft"), b: ("other", "unchanged")},
            settings_drafts={str(a): {"key": "draft"}, str(b): {"other": "kept"}},
            settings_file_paths={str(a): "C:\\APP.INI", str(b): "C:\\OTHER.INI"},
            settings_file_drafts={
                (str(a), "C:\\APP.INI"): ("revision", "unsaved"),
                (str(b), "C:\\OTHER.INI"): ("other", "kept"),
            },
            browse_locations={str(a): "C:\\APPS", str(b): "D:\\OTHER"},
            icon_cache={(str(a), "app", "record"): "first", (str(b), "other", "record"): "other"},
            icon_requests={(str(a), "app"), (str(b), "other")},
            undo_commands=[{"ball": a, "undo": "a"}, {"ball": b, "undo": "b"}],
            redo_commands=[{"ball": a, "redo": "a"}, {"ball": b, "redo": "b"}],
            last_log=a / "logs/run.log",
            last_result=7,
            log_text="previous output",
            install_summary="receipt",
        )

    def test_targets_only_real_package_directories_and_checks_identity(self):
        self.assertEqual(lifecycle.package(self.source), self.source)
        for target in (self.root, Path.home(), Path("/")):
            with self.assertRaises((ValueError, OSError)):
                lifecycle.package(target)
        linked = self.root / "Linked.2ineball"
        linked.symlink_to(self.source, target_is_directory=True)
        with self.assertRaises(ValueError):
            lifecycle.package(linked)
        manifest = self.source / "ball.json"
        manifest.unlink()
        manifest.symlink_to(self.other / "ball.json")
        with self.assertRaises(ValueError):
            lifecycle.package(self.source)
        expected = lifecycle.identity(self.other)
        moved = self.other.with_name("Moved.2ineball")
        self.other.rename(moved)
        self.make_ball("Other")
        with self.assertRaisesRegex(ValueError, "changed on disk"):
            lifecycle.package(self.other, expected)

    def test_rename_keeps_extension_and_refuses_existing_or_escaping_names(self):
        self.assertEqual(
            lifecycle.rename_destination(self.source, "New"), self.root / "New.2ineball"
        )
        self.assertEqual(
            lifecycle.rename_destination(self.source, "New.2ineball"), self.root / "New.2ineball"
        )
        for name in (
            "",
            ".",
            "..",
            ".2ineball",
            "../outside",
            "a/b",
            "a\\b",
            "a\x00b",
            "a\nb",
            "Other",
        ):
            with self.assertRaises(ValueError):
                lifecycle.rename_destination(self.source, name)
        dangling = self.root / "Dangling.2ineball"
        dangling.symlink_to(self.root / "Missing")
        with self.assertRaises(ValueError):
            lifecycle.rename_destination(self.source, "Dangling")
        self.assertTrue(self.source.exists())
        self.assertTrue(self.other.exists())

    def test_rename_rekeys_local_drafts_and_log_without_changing_another_ball(self):
        window = self.window()
        target = self.root / "Renamed.2ineball"
        lifecycle.update_references(window, self.source, target)
        self.assertEqual(window.ball, target)
        self.assertEqual(window.drafts[target], ("base", "draft"))
        self.assertEqual(
            window.settings_file_drafts[(str(target), "C:\\APP.INI")], ("revision", "unsaved")
        )
        self.assertEqual(window.settings_drafts[str(target)], {"key": "draft"})
        self.assertEqual(window.browse_locations[str(target)], "C:\\APPS")
        self.assertEqual(window.last_log, target / "logs/run.log")
        self.assertEqual(window.drafts[self.other], ("other", "unchanged"))
        self.assertEqual(window.settings_drafts[str(self.other)], {"other": "kept"})
        self.assertEqual(window.icon_cache, {(str(self.other), "other", "record"): "other"})
        self.assertEqual(window.undo_commands, [{"ball": self.other, "undo": "b"}])
        self.assertTrue(self.source.exists())

    def test_removing_an_unselected_ball_preserves_selection_and_other_drafts(self):
        window = self.window(selected=self.other)
        lifecycle.update_references(window, self.source)
        self.assertEqual(window.ball, self.other)
        self.assertEqual(window.external, [self.other])
        self.assertNotIn(self.source, window.drafts)
        self.assertNotIn(str(self.source), window.settings_file_paths)
        self.assertNotIn((str(self.source), "C:\\APP.INI"), window.settings_file_drafts)
        self.assertEqual(window.drafts[self.other], ("other", "unchanged"))
        self.assertTrue(self.other.exists())

    def test_removing_selected_ball_clears_only_its_activity(self):
        window = self.window()
        lifecycle.update_references(window, self.source)
        self.assertIsNone(window.ball)
        self.assertIsNone(window.snapshot)
        self.assertIsNone(window.last_log)
        self.assertIsNone(window.last_result)
        self.assertEqual(window.log_text, "")
        self.assertEqual(window.install_summary, "")
        self.assertIn(self.other, window.drafts)
