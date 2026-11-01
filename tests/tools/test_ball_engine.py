# Public domain. No rights reserved. Wholly AI-authored.
"""Shared C ABI: lossless metadata, hashlib parity, containment and concurrency."""

import concurrent.futures
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tools import balls


class EngineTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="2ine-engine-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.ball = self.root / "Ball with café and spaces.2ineball"
        balls.init(self.ball)

    def test_large_unknown_numbers_and_nested_metadata_are_lossless(self):
        data = balls.load(self.ball)
        data["future"] = {
            "integer": 18446744073709551615,
            "negative": -9007199254740993,
            "nested": [True, None, "café"],
        }
        balls.save(self.ball / "ball.json", data)
        balls.main(["--ball", str(self.ball), "config", "env.LANG", "de_DE"])
        self.assertEqual(balls.load(self.ball)["future"], data["future"])

    def test_runtime_identity_matches_independent_sha256(self):
        build = self.root / "build"
        build.mkdir()
        for name, content in (
            ("lx_loader", b"loader"),
            ("libexample.so", bytes(range(256))),
            ("libexample.dylib", b"dynamic"),
        ):
            (build / name).write_bytes(content)
        digest = hashlib.sha256()
        for path in sorted(build.iterdir()):
            digest.update(path.name.encode())
            digest.update(hashlib.sha256(path.read_bytes()).digest())
        env = balls.environment(self.ball, balls.load(self.ball), build, "unicorn")
        self.assertEqual(env["TWOINE_RUNTIME_ID"], digest.hexdigest())

    def test_nonexistent_traversal_cannot_escape_mapped_roots(self):
        with self.assertRaisesRegex(ValueError, "outside mapped"):
            balls.host_path(r"C:\missing\..\..\escape.ini", self.ball, balls.load(self.ball))

    def test_independent_calls_are_reentrant(self):
        # Initialize the ctypes binding before issuing simultaneous C calls.
        balls.load(self.ball)

        def create(index):
            path = self.root / f"Parallel {index}"
            balls.init(path)
            snapshot = balls.native_request({"action": "snapshot", "ball": str(path)})
            return json.dumps(snapshot["manifest"], sort_keys=True)

        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(create, range(24)))
        self.assertEqual(len(set(results)), 1)
