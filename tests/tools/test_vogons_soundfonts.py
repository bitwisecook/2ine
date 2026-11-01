# Public domain. No rights reserved. Wholly AI-authored.
"""Forum discovery separates post licenses from download claims and fetches one link."""

from __future__ import annotations

import hashlib
import io
import json
import tempfile
import unittest
import urllib.error
import zipfile
from email.message import Message
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from tests.tools.test_soundfonts import soundfont
from tools import vogons_soundfonts as forum

TOPIC = "https://www.vogons.org/viewtopic.php?t=88648"
DRIVE = "https://drive.google.com/file/d/selected123/view?usp=sharing"
PAGE = f"""<!doctype html><html><h2 id="title">A sampled bank</h2>
<section id="p100" class="post"><div class="post_header">
<a class="post_license_button" title="CC-BY-4.0" href="https://example.invalid/post-license">license</a>
<time datetime="2022-07-09T12:00:00Z">date</time></div>
<address class="post_user_username"><a class="username">Author &amp; Artist</a><span>Offline</span></address>
<div class="post_content"><p>Bank v0.5<br><a href="{DRIVE}">Download bank</a></p>
<p>Author settings are text: print('data only')</p>
<figure><a href="./download/file.php?id=77&amp;mode=view">diagram.png</a>
<span class="attachment_license">Public domain</span></figure>
<a href="javascript:invalid()">not a link</a></div></section></html>"""


class Response(io.BytesIO):
    def __init__(self, data, url, content_type="text/html", name=None):
        super().__init__(data)
        self.url = url
        self.headers = Message()
        self.headers["Content-Type"] = content_type
        if name:
            self.headers["Content-Disposition"] = f'attachment; filename="{name}"'

    def geturl(self):
        return self.url


class ForumTests(unittest.TestCase):
    def inventory(self):
        page = forum.parse_page(PAGE, TOPIC)
        return {"topic": TOPIC, "links": page["posts"][0]["links"]}

    def test_actual_markup_shape_preserves_source_and_separates_license_claims(self):
        result = forum.parse_page(PAGE, TOPIC)
        self.assertEqual(result["title"], "A sampled bank")
        post = result["posts"][0]
        self.assertEqual(post["author"], "Author & Artist")
        self.assertEqual(post["date"], "2022-07-09T12:00:00Z")
        self.assertEqual(post["post_license_claims"][0]["claim"], "CC-BY-4.0")
        self.assertEqual(post["body_license_claims"], ["Public domain"])
        self.assertIn("print('data only')", post["text"])
        self.assertEqual(len(post["links"]), 2)
        self.assertEqual(post["links"][0]["kind"], "google-drive-file")
        self.assertIn("Bank v0.5", post["links"][0]["context"])
        self.assertTrue(post["links"][0]["bank_license"].startswith("unknown"))
        self.assertEqual(post["links"][1]["attachment_license_claims"], ["Public domain"])
        self.assertEqual(
            post["links"][1]["url"], "https://www.vogons.org/download/file.php?id=77&mode=view"
        )

    def test_denied_urllib_page_can_use_ordinary_public_curl(self):
        denied = urllib.error.HTTPError(TOPIC, 403, "Forbidden", Message(), None)
        result = SimpleNamespace(returncode=0, stdout=PAGE.encode(), stderr=b"")
        with (
            patch.object(forum.urllib.request, "urlopen", side_effect=denied),
            patch.object(forum.shutil, "which", return_value="/usr/bin/curl"),
            patch.object(forum.subprocess, "run", return_value=result) as run,
        ):
            self.assertEqual(forum.fetch_page(TOPIC), PAGE.encode())
            command = run.call_args.args[0]
            self.assertEqual(command[-1], TOPIC)
            self.assertIn("--max-filesize", command)
            self.assertNotIn("--cookie", command)

    def test_pagination_is_topic_bound_deduplicated_and_cap_is_explicit(self):
        first = PAGE.replace(
            "</html>",
            '<a href="./viewtopic.php?t=88648&amp;start=20">Next</a>'
            '<a href="./viewtopic.php?t=999&amp;start=20">Other</a></html>',
        )
        second = PAGE.replace('id="p100"', 'id="p200"')
        calls = []

        def get(request, timeout):
            calls.append(request.full_url)
            data = second if "start=" in request.full_url else first
            return Response(data.encode(), request.full_url)

        with (
            tempfile.TemporaryDirectory() as temporary,
            patch.object(forum.urllib.request, "urlopen", get),
        ):
            output = Path(temporary)
            result = forum.discover(TOPIC, output, max_pages=1)
            self.assertFalse(result["complete"])
            self.assertEqual(len(result["unfetched_pages"]), 1)
            result = forum.discover(TOPIC, output, max_pages=3)
            self.assertTrue(result["complete"])
            self.assertEqual(len(result["posts"]), 2)
            self.assertEqual(len(result["links"]), 2)
            self.assertEqual(len(result["links"][0]["observations"]), 2)
            self.assertTrue(all("t=88648" in url for url in calls))
            self.assertEqual(
                json.loads((output / "inventory.json").read_text())["title"], "A sampled bank"
            )

    def test_normal_public_drive_confirmation_then_real_bank_receipt(self):
        warning = b"""<form action="https://drive.usercontent.google.com/download">
<input name="id" value="selected123"><input name="confirm" value="t">
<input name="uuid" value="public-token"></form>"""
        bank = soundfont()
        requests = []

        def get(request, timeout):
            requests.append(request.full_url)
            if len(requests) == 1:
                return Response(warning, request.full_url)
            return Response(bank, request.full_url, "application/octet-stream", "bank.sf2")

        with tempfile.TemporaryDirectory() as temporary:
            receipt = forum.selected_fetch(
                self.inventory(),
                hashlib.sha256(DRIVE.encode()).hexdigest()[:16],
                Path(temporary),
                opener=get,
            )
            self.assertEqual(len(requests), 2)
            self.assertIn("drive.google.com/uc?", requests[0])
            self.assertIn("drive.usercontent.google.com/download?", requests[1])
            self.assertEqual(receipt["format"], "sf2")
            self.assertTrue(receipt["complete_gm"])
            self.assertEqual(receipt["sha256"], hashlib.sha256(bank).hexdigest())
            self.assertEqual(Path(receipt["path"]).read_bytes(), bank)
            self.assertTrue(Path(receipt["path"]).with_name("receipt.json").exists())
            self.assertTrue(receipt["bank_license"].startswith("unknown"))

    def test_landing_page_caps_hash_and_nonprovider_forms_are_rejected(self):
        self.assertIsNone(
            forum.drive_confirmation(
                b'<form action="https://example.invalid/download"><input name="confirm" value="t"></form>',
                "https://drive.google.com/uc?id=selected123",
            )
        )
        self.assertIsNone(
            forum.drive_confirmation(
                b'<form action="https://drive.google.com/uc"><input name="id" value="other"><input name="confirm" value="t"></form>',
                "https://drive.google.com/uc?id=selected123",
            )
        )
        identity = hashlib.sha256(DRIVE.encode()).hexdigest()[:16]
        for kind in ("html", "cap", "hash"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                data = b"<html>login required</html>" if kind == "html" else soundfont()
                content = "text/html" if kind == "html" else "application/octet-stream"

                def get(request, timeout, data=data, content=content):
                    return Response(data, request.full_url, content)

                with self.assertRaises(ValueError):
                    forum.selected_fetch(
                        self.inventory(),
                        identity,
                        root,
                        max_bytes=1 if kind == "cap" else 1024**2,
                        expected_sha256="0" * 64 if kind == "hash" else None,
                        opener=get,
                    )
                self.assertEqual(list(root.iterdir()), [])

    def test_zip_only_selected_member_is_extracted_with_bounds_and_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "bank.zip"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("fonts/bank.sf2", soundfont())
                archive.writestr("../escape", b"no")
            receipt = {
                "path": str(path),
                "sha256": forum.sf.digest(path),
                "bank_license": "unknown",
            }
            self.assertEqual(forum.inspect_download(path)["format"], "zip")
            out = forum.extract_member(receipt, "fonts/bank.sf2", root / "out", 1024**2)
            self.assertEqual(out.read_bytes(), soundfont())
            self.assertTrue(out.with_suffix(".sf2.receipt.json").exists())
            with self.assertRaises(ValueError):
                forum.extract_member(receipt, "../escape", root / "out", 1024**2)
            self.assertFalse((root / "escape").exists())


if __name__ == "__main__":
    unittest.main()
