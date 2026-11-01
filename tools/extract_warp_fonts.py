#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Extract native Warp outline fonts from user-supplied installation media."""

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

FONTS = tuple(
    name + ".PFB"
    for name in (
        "HELV",
        "HELVB",
        "HELVI",
        "HELVBI",
        "TNR",
        "TNRB",
        "TNRI",
        "TNRBI",
        "COUR",
        "COURB",
        "COURI",
        "COURBI",
    )
)


def select_members(listing):
    members = {}
    for line in listing.splitlines():
        if not line.startswith("Path = "):
            continue
        member = line[7:].replace("\\", "/")
        match = re.fullmatch(r"os2image/disk_[0-9]+/([^/]+)", member, re.IGNORECASE)
        if match and (name := match[1].upper()) in FONTS:
            if name in members:
                raise ValueError(f"ambiguous ISO font: {name}")
            members[name] = member
    missing = set(FONTS) - members.keys()
    if missing:
        raise ValueError(f"missing native Warp fonts: {', '.join(sorted(missing))}")
    return members


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def extract(iso, destination, seven_zip, deark):
    checkout = Path(__file__).resolve().parents[1]
    destination = destination.resolve()
    if destination.is_relative_to(checkout):
        raise ValueError("choose a private font directory outside the 2ine checkout")
    if destination.exists():
        raise ValueError("destination must not exist; existing fonts are never overwritten")
    listing = subprocess.check_output([seven_zip, "l", "-slt", str(iso)], text=True)
    members = select_members(listing)
    with tempfile.TemporaryDirectory(prefix="2ine-warp-fonts-") as scratch:
        root = Path(scratch)
        decoded = {}
        records = []
        for name in FONTS:
            packed = root / name
            with packed.open("wb") as output:
                subprocess.run(
                    [seven_zip, "e", "-so", str(iso), members[name]], stdout=output, check=True
                )
            unpacked = root / name.removesuffix(".PFB")
            unpacked.mkdir()
            subprocess.run([deark, "-q", "-od", str(unpacked), str(packed)], check=True)
            candidates = [
                path
                for path in unpacked.iterdir()
                if path.is_file() and path.name.upper().endswith(("." + name, "_" + name))
            ]
            if len(candidates) != 1:
                raise ValueError(f"expected exactly one decoded {name}")
            font = candidates[0]
            if font.read_bytes()[:2] != b"\x80\x01":
                raise ValueError(f"decoded {name} is not a Type 1 PFB font")
            decoded[name] = font
            records.append(
                {
                    "file": name,
                    "iso_member": members[name],
                    "packed_sha256": sha256(packed),
                    "sha256": sha256(font),
                    "bytes": font.stat().st_size,
                }
            )
        manifest = {
            "iso": iso.name,
            "iso_sha256": sha256(iso),
            "fonts": records,
            "notice": "Original proprietary fonts. Retain privately; do not redistribute.",
        }
        destination.mkdir(parents=True)
        for name, source in decoded.items():
            (destination / name).write_bytes(source.read_bytes())
        (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--7z", dest="seven_zip", default="7z")
    parser.add_argument("--deark", default="deark")
    args = parser.parse_args()
    try:
        result = extract(args.iso.resolve(), args.destination, args.seven_zip, args.deark)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")
    print(f"Extracted {len(result['fonts'])} private fonts to {args.destination.resolve()}")


if __name__ == "__main__":
    main()
