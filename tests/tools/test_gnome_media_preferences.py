# Public domain. No rights reserved. Wholly AI-authored.
"""Remember only verified OS/2 media without coupling preferences to a ball."""

import tempfile
import unittest
from pathlib import Path

from apps.gnome.twoine_gnome import media_preferences as media


class MediaPreferencesTests(unittest.TestCase):
    def test_new_ball_reuses_global_but_existing_ball_keeps_its_source(self):
        preferences = {"lastOS2MediaSources": ["/media/server.zip!/disk.iso!/os2image"]}
        self.assertEqual(media.preferred({}, preferences), preferences["lastOS2MediaSources"])
        snapshot = {"os2Support": {"mediaSources": ["/media/warp.iso!/os2image"]}}
        self.assertEqual(
            media.preferred(snapshot, preferences), snapshot["os2Support"]["mediaSources"]
        )
        self.assertEqual(
            preferences, {"lastOS2MediaSources": ["/media/server.zip!/disk.iso!/os2image"]}
        )

    def test_validation_retains_opaque_normalized_paths_and_physical_picker_paths(self):
        preferences = {"otherPreference": True}
        sources = [
            "/media/server.zip!/WSEB_CP2.iso!/OS2IMAGE",
            "/media/server.zip!/WSEB_CP2.iso!/CID/SERVER/MPTS/MPTN",
        ]
        self.assertTrue(
            media.remember(
                preferences,
                {"complete": True, "sources": sources, "physicalSources": ["/media/server.zip"]},
            )
        )
        self.assertEqual(preferences["lastOS2MediaSources"], sources)
        self.assertEqual(preferences["lastOS2MediaFiles"], ["/media/server.zip"])
        self.assertTrue(preferences["otherPreference"])

    def test_incomplete_mixed_or_invalid_media_never_overwrites_success(self):
        preferences = {"lastOS2MediaSources": ["valid"], "lastOS2MediaFiles": ["physical"]}
        before = dict(preferences)
        for result in (
            {
                "complete": False,
                "sources": ["invalid"],
                "media": [{"status": "verified-os2-media"}],
            },
            {"sources": ["unchecked"]},
            {"complete": True, "sources": []},
        ):
            self.assertFalse(media.remember(preferences, result))
            self.assertEqual(preferences, before)

    def test_picker_uses_existing_physical_sources_without_parsing_virtual_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "disk.zip"
            archive.touch()
            self.assertEqual(
                media.picker_path({"lastOS2MediaFiles": ["/does/not/exist", str(archive)]}), archive
            )
            self.assertEqual(media.picker_path({"lastOS2MediaFiles": [temporary]}), Path(temporary))
            self.assertIsNone(
                media.picker_path({"lastOS2MediaSources": [str(archive) + "!/disk.iso"]})
            )

    def test_forget_removes_only_global_media_and_keeps_chooser_possible(self):
        preferences = {
            "lastOS2MediaSources": ["missing"],
            "lastOS2MediaFiles": ["missing"],
            "ballPaths": ["ball"],
        }
        self.assertTrue(media.forget(preferences))
        self.assertEqual(preferences, {"ballPaths": ["ball"]})
        self.assertFalse(media.forget(preferences))
        self.assertEqual(media.preferred({}, preferences), [])

    def test_malformed_values_are_filtered_and_duplicate_sources_are_stable(self):
        self.assertEqual(
            media.sources(["disk", "disk", None, 10, "", "a\0b", "other"]), ["disk", "other"]
        )
        self.assertEqual(media.sources("disk"), [])
        self.assertTrue(media.remember({}, {"complete": True, "sources": ["disk"]}))


if __name__ == "__main__":
    unittest.main()
