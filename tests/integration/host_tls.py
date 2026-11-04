#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Ephemeral test identity; no network access, persistent keys or fixed ports."""
import subprocess
import sys
import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory(prefix="twoine-tls-") as directory:
    cert, key = Path(directory) / "cert.pem", Path(directory) / "key.pem"
    subprocess.run(
        [sys.argv[2], "req", "-x509", "-newkey", "rsa:2048", "-noenc", "-days", "1",
         "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
         "-keyout", str(key), "-out", str(cert)],
        check=True, capture_output=True, timeout=15,
    )
    subprocess.run([sys.argv[1], str(cert), str(key)], check=True, timeout=20)
