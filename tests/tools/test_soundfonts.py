# Public domain. No rights reserved. Wholly AI-authored.
"""Pinned downloads never select corrupt banks or discard unrelated settings."""

from __future__ import annotations

import hashlib
import io
import json
import struct
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch

from tools import fetch_soundfonts as sf


def soundfont(programs: int = 128) -> bytes:
    presets = bytearray()
    for number in range(programs):
        presets.extend(struct.pack("<20sHHHIII", b"Program", number, 0, 0, 0, 0, 0))
    presets.extend(struct.pack("<20sHHHIII", b"Drums", 0, 128, 0, 0, 0, 0))
    presets.extend(struct.pack("<20sHHHIII", b"EOP", 0, 0, 0, 0, 0, 0))
    pdta = b"pdta" + struct.pack("<4sI", b"phdr", len(presets)) + presets
    body = b"sfbk" + struct.pack("<4sI", b"LIST", len(pdta)) + pdta
    return b"RIFF" + struct.pack("<I", len(body)) + body


def artifact(name: str, data: bytes) -> sf.Artifact:
    return sf.Artifact(
        name, "https://example.invalid/" + name, len(data), hashlib.sha256(data).hexdigest()
    )


class SoundfontTests(unittest.TestCase):
    def test_checked_download_cached_and_repaired_atomically(self):
        content = b"bank bytes" * 100
        item = artifact("bank.sf2", content)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            calls = []

            def download(request, timeout):
                self.assertEqual(timeout, 60)
                calls.append(request.full_url)
                return io.BytesIO(content)

            destination = sf.fetch_artifact(item, root, download)
            self.assertEqual(destination.read_bytes(), content)
            sf.fetch_artifact(item, root, download)
            self.assertEqual(len(calls), 1)
            destination.write_bytes(b"corrupt")
            sf.fetch_artifact(item, root, download)
            self.assertEqual(destination.read_bytes(), content)
            self.assertEqual(len(calls), 2)
            self.assertEqual(list(root.iterdir()), [destination])

    def test_mismatch_partial_and_oversize_leave_no_partial_download(self):
        item = artifact("bank.sf2", b"expected bytes")
        for received in (b"short", b"x" * item.size, b"x" * (item.size + 1)):
            with self.subTest(received=received), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                destination = root / item.name
                destination.write_bytes(b"old entry")
                with self.assertRaises(ValueError):
                    sf.fetch_artifact(
                        item, root, lambda *args, data=received, **kwargs: io.BytesIO(data)
                    )
                self.assertEqual(destination.read_bytes(), b"old entry")
                self.assertEqual(list(root.iterdir()), [destination])

    def test_preset_inventory_rejects_partial_and_malformed_bank(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "bank.sf2"
            path.write_bytes(soundfont())
            sf.validate_sf2(path)
            for data in (soundfont(127), soundfont()[:-1], b"not an SF2"):
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    sf.validate_sf2(path)

    def test_selection_preserves_unknown_settings_and_requires_current_hash(self):
        data = soundfont()
        bank = sf.Bank(
            "bank",
            "Bank",
            "1",
            "Author",
            "GM",
            "https://example.invalid",
            "https://example.invalid/source",
            "MIT",
            "bank.sf2",
            (artifact("bank.sf2", data), artifact("LICENSE", b"MIT")),
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path, config = root / "bank.sf2", root / "audio.conf"
            path.write_bytes(data)
            config.write_text(
                "# preserved\nmt32_control=/owned/control.rom\nmidi_backend=mt32\nsoundfont=/old\n"
            )
            sf.select_bank(bank, path, config)
            text = config.read_text()
            self.assertIn("# preserved\nmt32_control=/owned/control.rom\n", text)
            self.assertIn("midi_backend=gm\n", text)
            self.assertNotIn("midi_backend=mt32", text)
            self.assertIn(f"soundfont={path.resolve()}\n", text)
            self.assertIn(f"soundfont_sha256={hashlib.sha256(data).hexdigest()}\n", text)
            before = config.read_bytes()
            wrong = replace(bank, files=(replace(bank.files[0], sha256="0" * 64), bank.files[1]))
            with self.assertRaises(ValueError):
                sf.select_bank(wrong, path, config)
            self.assertEqual(config.read_bytes(), before)

    def test_catalog_rejects_traversal_insecure_urls_and_duplicate_ids(self):
        original = json.loads(sf.CATALOG.read_text())
        self.assertEqual(sf.load_catalog()[1], "generaluser-gs")
        for mutation in ("filename", "url", "duplicate"):
            raw = json.loads(json.dumps(original))
            if mutation == "filename":
                raw["banks"][0]["files"][0]["name"] = "../escape.sf2"
            elif mutation == "url":
                raw["banks"][0]["files"][0]["url"] = "http://example.invalid/file"
            else:
                raw["banks"].append(raw["banks"][0])
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary) / "catalog.json"
                path.write_text(json.dumps(raw))
                with self.assertRaises(ValueError):
                    sf.load_catalog(path)

    def test_user_bank_import_is_content_addressed_and_keeps_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "My bank.sf2"
            source.write_bytes(soundfont())
            cache, config = root / "cache", root / "audio.conf"
            result = sf.use_file(source, cache, config)
            sha = hashlib.sha256(source.read_bytes()).hexdigest()
            self.assertEqual(result, cache / "user" / sha / "bank.sf2")
            self.assertEqual(result.read_bytes(), source.read_bytes())
            receipt = json.loads(result.with_name("receipt.json").read_text())
            self.assertEqual(receipt["origin"], "user-supplied")
            self.assertEqual(receipt["source"], str(source.resolve()))
            self.assertEqual(receipt["sha256"], sha)
            self.assertIn("soundfont_id=user-" + sha[:16], config.read_text())
            self.assertIn("midi_backend=gm", config.read_text())
            before = config.read_bytes()
            source.write_bytes(soundfont(127))
            with self.assertRaises(ValueError):
                sf.use_file(source, cache, config)
            self.assertEqual(config.read_bytes(), before)

    def test_config_environment_precedence(self):
        with patch.dict(
            "os.environ",
            {"TWOINE_AUDIO_CONFIG": "/chosen/audio.conf", "XDG_CONFIG_HOME": "/ignored"},
        ):
            self.assertEqual(sf.default_config(), Path("/chosen/audio.conf"))


if __name__ == "__main__":
    unittest.main()
