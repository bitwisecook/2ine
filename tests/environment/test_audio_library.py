# Public domain. No rights reserved. Wholly AI-authored.
"""Shared bank persistence, folder identity, archive limits and opaque metadata."""

import copy
import json
import os
import tempfile
import unittest
import zipfile
from pathlib import Path

from tools.ball import engine as ball_api
from tools.codegen import project_sources


class AudioLibraryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="2ine-audio-library-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.ball = self.root / "Audio.2ineball"
        ball_api.init(self.ball)
        self.instrument = self.root / "Instrument"
        self.instrument.mkdir()
        self.sfz = self.instrument / "Piano.sfz"
        self.sfz.write_text("<region> sample=tone.wav\n")
        (self.instrument / "tone.wav").write_bytes(b"original sample")
        (self.instrument / "LICENSE.txt").write_text("Original test input; public domain.\n")

    def request(self, action, **fields):
        return ball_api.native_request({"action": action, "ball": str(self.ball), **fields})

    def test_folder_contents_determine_identity_and_existing_install_is_preserved(self):
        first = self.request("import-soundfont", path=str(self.sfz))
        record = first["banks"][-1]
        installed = self.ball / record["file"]
        self.assertEqual((installed.parent / "tone.wav").read_bytes(), b"original sample")
        self.assertTrue((installed.parent / "LICENSE.txt").is_file())
        same = self.request("import-soundfont", path=str(self.sfz))
        self.assertEqual(len(same["banks"]), 2)
        (self.instrument / "tone.wav").write_bytes(b"new sample version")
        changed = self.request("import-soundfont", path=str(self.sfz))
        self.assertEqual(len(changed["banks"]), 3)
        self.assertNotEqual(record["id"], changed["banks"][-1]["id"])
        self.assertEqual((installed.parent / "tone.wav").read_bytes(), b"original sample")

    def test_save_preserves_unknown_fields_and_invalid_mapping_does_not_write(self):
        settings = self.request("audio-snapshot")["settings"]
        settings["future"] = {"kind": "kept"}
        settings["channels"][0]["future"] = "channel metadata"
        self.request("save-audio", settings=settings)
        older_client = copy.deepcopy(settings)
        del older_client["future"]
        del older_client["channels"][0]["future"]
        older_client["profile"] = "gs"
        saved = self.request("save-audio", settings=older_client)["settings"]
        self.assertEqual(saved["future"], settings["future"])
        self.assertEqual(saved["channels"][0]["future"], "channel metadata")
        before = (self.ball / "ball.json").read_bytes()
        for field, value in (
            ("target", 0),
            ("program", 128),
            ("bankNumber", 16384),
            ("percussion", 2),
            ("target", 1.5),
            ("muted", "false"),
        ):
            invalid = copy.deepcopy(saved)
            invalid["channels"][0][field] = value
            with self.assertRaises(ValueError):
                self.request("save-audio", settings=invalid)
            self.assertEqual((self.ball / "ball.json").read_bytes(), before)

    def test_zip_installs_nested_samples_and_license_and_rejects_traversal(self):
        archive = self.root / "Instrument.zip"
        with zipfile.ZipFile(archive, "w") as output:
            for path in self.instrument.iterdir():
                output.write(path, "Piano/" + path.name)
        result = self.request("import-soundfont", path=str(archive))
        installed = self.ball / result["banks"][-1]["file"]
        self.assertEqual(installed.read_bytes(), self.sfz.read_bytes())
        self.assertTrue((installed.parent / "tone.wav").is_file())
        self.assertTrue((installed.parent / "LICENSE.txt").is_file())
        before = (self.ball / "ball.json").read_bytes()
        with zipfile.ZipFile(archive, "w") as output:
            output.writestr("../escaped.sfz", self.sfz.read_bytes())
        with self.assertRaises(ValueError):
            self.request("import-soundfont", path=str(archive))
        self.assertEqual((self.ball / "ball.json").read_bytes(), before)
        self.assertFalse((self.root / "escaped.sfz").exists())

    @unittest.skipIf(os.name == "nt", "POSIX symlink fixture")
    def test_symlinks_in_instrument_folder_are_rejected_without_installing(self):
        (self.instrument / "link.wav").symlink_to(self.sfz)
        with self.assertRaisesRegex(ValueError, "links"):
            self.request("import-soundfont", path=str(self.sfz))
        self.assertEqual(len(self.request("audio-snapshot")["banks"]), 1)

    def test_wrong_riff_type_is_rejected(self):
        invalid = self.root / "fake.sf2"
        invalid.write_bytes(b"RIFF\x04\x00\x00\x00WAVE")
        with self.assertRaisesRegex(ValueError, "Invalid"):
            self.request("import-soundfont", path=str(invalid))

    def test_catalogue_links_credits_and_generated_files_agree(self):
        catalogue = ball_api.native_request({"action": "project-sources"})
        self.assertIn("Ryan C. Gordon", catalogue["originCredit"])
        projects = catalogue["projects"]
        self.assertGreater(len(projects), 50)
        self.assertEqual(len({item["id"] for item in projects}), len(projects))
        for item in projects:
            self.assertTrue(item["version"], item["id"])
            self.assertTrue(item["authors"], item["id"])
            for key in ("source", "download", "homepage"):
                self.assertTrue(item[key].startswith(("https://", "http://")), item["id"])
        for path, content in project_sources.generate().items():
            self.assertEqual(path.read_text(), content)
        raw = json.loads(project_sources.CATALOGUE.read_text())
        author = next(item for item in raw["projects"] if item["id"] == "sha256")
        self.assertIn("Brad Conte", author["authors"])


class ProjectSourceProviderTests(unittest.TestCase):
    def test_ffmpeg_records_the_linked_provider_and_license(self):
        with tempfile.TemporaryDirectory(prefix="2ine-video-catalogue-") as directory:
            root = Path(directory)
            metadata = {
                "version": "9.0.2-test",
                "license": "GPL version 3 or later",
                "provider": "system",
                "configuration": "--enable-gpl --enable-version3",
            }
            (root / "ffmpeg-build.json").write_text(json.dumps(metadata))
            catalogue = project_sources.installed_catalogue([root / "lx_loader"])
            self.assertEqual(catalogue["multimediaBuild"], metadata)
            entry = next(item for item in catalogue["projects"] if item["id"] == "ffmpeg")
            self.assertEqual(entry["version"], metadata["version"])
            self.assertEqual(entry["license"], metadata["license"])
            self.assertEqual(entry["provider"], metadata["provider"])
            self.assertEqual(entry["buildConfiguration"], metadata["configuration"])


if __name__ == "__main__":
    unittest.main()
