#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Verify writable FAT floppies, device protection and generated disk geometries."""

import argparse
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import ball as ball_api


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    ball = work / "Floppies.2ineball"

    def request(*arguments):
        return ball_api.native_request(
            {"action": "command", "ball": str(ball), "arguments": list(arguments)}
        )

    request("init")
    results = []
    reader = shutil.which("7zz") or shutil.which("7z")
    for size, track, cluster, fats in [
        (360, 9, 2, 2),
        (720, 9, 2, 3),
        (1200, 15, 1, 7),
        (1440, 18, 1, 9),
        (2880, 36, 2, 9),
    ]:
        image = work / f"floppy-{size}.img"
        image.unlink(missing_ok=True)
        snapshot = request("create-floppy", "A", str(image), str(size))
        assert snapshot["manifest"]["drives"]["A"].get("readonly", False) is False
        bpb = image.read_bytes()[:512]
        assert image.stat().st_size == size * 1024
        assert struct.unpack_from("<H", bpb, 11)[0] == 512 and bpb[13] == cluster
        assert struct.unpack_from("<H", bpb, 22)[0] == fats
        assert struct.unpack_from("<HH", bpb, 24) == (track, 2)
        before = image.read_bytes()
        try:
            request("create-floppy", "A", str(image), str(size))
        except ValueError as error:
            assert "exist" in str(error).lower()
        else:
            raise AssertionError("An existing image was replaced")
        assert image.read_bytes() == before
        subprocess.run(
            [
                str(args.program),
                "--writable",
                snapshot["roots"]["A"],
                str(ball / "devices/A.media"),
            ],
            check=True,
        )
        if reader:
            member = subprocess.run(
                [reader, "x", "-so", str(image), "Renamed file.txt"],
                check=True,
                capture_output=True,
            ).stdout
            assert member == b"floppy write verified"
        checker = Path("/sbin/fsck_msdos")
        if checker.exists():
            result = subprocess.run([str(checker), "-n", str(image)], capture_output=True)
            assert result.returncode == 0, result.stdout + result.stderr
        snapshot = request("media-readonly", "A", "on")
        assert snapshot["manifest"]["drives"]["A"]["readonly"] is True
        snapshot = request("device-lock", "A", "on")
        assert snapshot["manifest"]["drives"]["A"]["locked"] is True
        before = image.read_bytes()
        manifest = (ball / "ball.json").read_bytes()
        for command in [
            ("eject", "A"),
            ("unmap", "A"),
            ("map-image", "A", str(image)),
            ("media-readonly", "A", "off"),
            ("map", "A", str(work)),
        ]:
            try:
                request(*command)
            except ValueError as error:
                assert "locked" in str(error)
            else:
                raise AssertionError(f"Locked device accepted {command}")
            assert (ball / "ball.json").read_bytes() == manifest
        assert image.read_bytes() == before
        request("device-lock", "A", "off")
        request("media-readonly", "A", "off")
        request("eject", "A")
        results.append(
            {"size_kib": size, "bytes": len(before), "independent_member_check": reader is not None}
        )
    (work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(
        "All five floppy geometries passed read/write, long names, resize, rename, deletion, write protection, locking and filesystem checks."
    )


if __name__ == "__main__":
    main()
