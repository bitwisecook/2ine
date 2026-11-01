# Public domain. No rights reserved. Wholly AI-authored.
"""Named recommendations preserve source identity and never imply unavailable fetches."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import recommended_soundfonts as rec


class RecommendedTests(unittest.TestCase):
    def test_wiki_categories_and_exact_source_choices(self):
        index = rec.load_index()
        choices = {choice["id"]: choice for choice in index["choices"]}
        self.assertEqual(len(choices), 19)
        self.assertEqual(
            {choice["category"] for choice in choices.values()},
            {"high-quality", "sc55", "creative-ensoniq", "gus", "yamaha-xg"},
        )
        self.assertEqual(
            choices["sc55-patch93"]["source_url"], "https://musical-artifacts.com/artifacts/1228"
        )
        self.assertEqual(choices["voice-crystal"]["provider"], "source")
        self.assertEqual(choices["generaluser-gs"]["provider"], "catalog")
        self.assertEqual(choices["yamaha-xg-logue"]["provider"], "direct")

    def test_source_only_choice_reports_original_page_and_does_not_download(self):
        with patch.object(rec.external, "selected_fetch") as fetch:
            for identity in ("sc55-trevor", "sc55-patch93", "voice-crystal"):
                with (
                    self.subTest(identity=identity),
                    self.assertRaisesRegex(ValueError, "Source-only"),
                ):
                    rec.fetch_choice(rec.load_index(), identity, Path("unused"))
            fetch.assert_not_called()

    def test_existing_pinned_bank_provider_keeps_license_checked_fetch_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = root / "audio.conf"
            path = root / "bank.sf2"
            with (
                patch.object(rec.sf, "fetch_bank", return_value=path) as fetch,
                patch.object(rec.sf, "select_bank") as select,
            ):
                result = rec.fetch_choice(rec.load_index(), "generaluser-gs", root)
                self.assertEqual(result, path)
                select.assert_not_called()
                self.assertEqual(fetch.call_args.args[0].identity, "generaluser-gs")
                rec.fetch_choice(rec.load_index(), "generaluser-gs", root, config)
                select.assert_called_once()
                self.assertEqual(select.call_args.args[2], config)
                with self.assertRaisesRegex(ValueError, "byte limit"):
                    rec.fetch_choice(rec.load_index(), "generaluser-gs", root, max_bytes=1)

    def test_direct_external_file_gets_source_receipt_and_selection_is_explicit(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "bank.sf2"
            path.write_bytes(b"downloaded")
            receipt = {"path": str(path), "sha256": "identity", "format": "sf2"}
            with (
                patch.object(rec.external, "selected_fetch", return_value=receipt.copy()) as fetch,
                patch.object(rec.sf, "use_file") as select,
            ):
                self.assertEqual(rec.fetch_choice(rec.load_index(), "yamaha-xg-logue", root), path)
                select.assert_not_called()
                arguments = fetch.call_args.args
                self.assertIn("mt32-pi/wiki/Recommended-SoundFonts", arguments[0]["topic"])
                self.assertIn("raw.githubusercontent.com/logue", arguments[0]["links"][0]["url"])
                saved = json.loads((root / "receipt.json").read_text())
                self.assertTrue(saved["bank_license"].startswith("unknown"))
                rec.fetch_choice(rec.load_index(), "yamaha-xg-logue", root, root / "audio.conf")
                select.assert_called_once()

    def test_invalid_provider_cannot_be_executed_from_index(self):
        index = json.loads(rec.INDEX.read_text())
        index["choices"][0]["provider"] = "shell"
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "index.json"
            path.write_text(json.dumps(index))
            with self.assertRaises(ValueError):
                rec.load_index(path)


if __name__ == "__main__":
    unittest.main()
