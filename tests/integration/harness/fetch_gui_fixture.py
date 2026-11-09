#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Fetch the exact archived GUI fixture declared in gui-fixtures.json."""

import argparse
import hashlib
import io
import json
import urllib.request
import zipfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    fixture = json.loads(Path(__file__).with_name("gui-fixtures.json").read_text())["pmstars"]
    args.destination.mkdir(parents=True, exist_ok=True)
    target = args.destination / fixture["member"]
    if target.exists():
        assert hashlib.sha256(target.read_bytes()).hexdigest() == fixture["program_sha256"], target
    else:
        with urllib.request.urlopen(fixture["url"], timeout=30) as response:
            archive = response.read(1024 * 1024)
        assert hashlib.sha256(archive).hexdigest() == fixture["archive_sha256"], "archive checksum"
        with zipfile.ZipFile(io.BytesIO(archive)) as package:
            program = package.read(fixture["member"])
        assert hashlib.sha256(program).hexdigest() == fixture["program_sha256"], "program checksum"
        target.write_bytes(program)
    print(target.resolve())


if __name__ == "__main__":
    main()
