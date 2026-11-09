#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise FAT32, exFAT and HPFS images through the shared engine and guest host services."""

import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.ball import engine as ball_api


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--stress", action="store_true")
    parser.add_argument("--hpfs-checker", type=Path)
    parser.add_argument("--hpfs-formatter", type=Path)
    parser.add_argument("--exfat-checker", type=Path)
    parser.add_argument("--exfat-formatter", type=Path)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    ball = work / "Filesystems.2ineball"

    def request(action, **fields):
        return ball_api.native_request({"action": action, "ball": str(ball), **fields})

    def command(*arguments):
        return request("command", arguments=list(arguments))

    command("init")
    reference = work / "REFERENCE.BIN"
    reference.write_bytes(bytes(range(256)) * 37)
    results = []
    for filesystem, mib in [("fat32", 64), ("exfat", 32), ("hpfs", 64)]:
        image = work / (filesystem + ".img")
        image.unlink(missing_ok=True)
        snapshot = command("create-disk", "F", str(image), filesystem, str(mib))
        command("media-readonly", "F", "off")
        root = snapshot["roots"]["F"]
        subprocess.run(
            [str(args.program), "--writable", root, str(ball / "devices/F.media")], check=True
        )
        if args.stress:
            subprocess.run([str(args.program), "--fragmentation", root], check=True)
        if filesystem == "hpfs":
            subprocess.run([str(args.program), "--metadata", root], check=True)
        request("import-files", path="F:\\", sources=[str(reference)])
        entry = next(
            e for e in request("browse", path="F:\\")["entries"] if e["name"] == reference.name
        )
        command("media-readonly", "F", "on")
        before = digest(image)
        subprocess.run(
            [
                str(args.program),
                entry["hostPath"],
                str(reference),
                "NO NAME" if filesystem == "hpfs" else "",
                filesystem.upper(),
            ],
            check=True,
        )
        assert digest(image) == before
        independent = None
        if filesystem == "hpfs" and args.hpfs_checker:
            check = subprocess.run(
                [str(args.hpfs_checker), "check", "-p", "-s", "-u", str(image)],
                capture_output=True,
                text=True,
                check=True,
            )
            assert "total errors: 0" in check.stdout.lower() + check.stderr.lower(), (
                check.stdout + check.stderr
            )
            extracted = work / "hpfs-independent.bin"
            subprocess.run(
                [str(args.hpfs_checker), "copy", str(image), "\\REFERENCE.BIN", str(extracted)],
                capture_output=True,
                check=True,
            )
            assert extracted.read_bytes() == reference.read_bytes()
            independent = "fst check and byte extraction"
        if filesystem == "exfat" and args.exfat_checker:
            subprocess.run(
                [str(args.exfat_checker), "-n", str(image)], check=True, capture_output=True
            )
            independent = "exfatfsck"
        if filesystem == "fat32" and Path("/sbin/fsck_msdos").exists():
            subprocess.run(["/sbin/fsck_msdos", "-n", str(image)], check=True, capture_output=True)
            independent = "fsck_msdos"
        # The same filesystem must work inside a primary MBR partition.
        wrapped = work / (filesystem + "-partitioned.img")
        with wrapped.open("wb") as output, image.open("rb") as source:
            mbr = bytearray(512)
            mbr[446 + 4] = 0x0C if filesystem == "fat32" else 7
            struct.pack_into("<II", mbr, 446 + 8, 2048, image.stat().st_size // 512)
            mbr[510:512] = b"\x55\xaa"
            output.write(mbr)
            output.seek(2048 * 512)
            shutil.copyfileobj(source, output)
        prefix = wrapped.read_bytes()[: 2048 * 512]
        command("map-image", "F", str(wrapped))
        assert request("snapshot")["manifest"]["drives"]["F"]["readonly"] is True
        command("media-readonly", "F", "off")
        marker = work / (filesystem + "-marker.txt")
        marker.write_text("partition write verified\n")
        request("import-files", path="F:\\", sources=[str(marker)])
        assert any(e["name"] == marker.name for e in request("browse", path="F:\\")["entries"])
        with wrapped.open("rb") as source:
            assert source.read(len(prefix)) == prefix
        results.append(
            {
                "filesystem": filesystem,
                "bytes": image.stat().st_size,
                "source_sha256": before,
                "partition_read_write": True,
                "independent_check": independent,
            }
        )
    if args.hpfs_formatter:
        existing = work / "external-hpfs.img"
        with existing.open("wb") as output:
            output.truncate(64 * 1024 * 1024)
        subprocess.run(
            [str(args.hpfs_formatter), "-i", str(existing)], check=True, capture_output=True
        )
        snapshot = command("map-image", "F", str(existing))
        command("media-readonly", "F", "off")
        subprocess.run(
            [
                str(args.program),
                "--writable",
                snapshot["roots"]["F"],
                str(ball / "devices/F.media"),
            ],
            check=True,
        )
        if args.hpfs_checker:
            check = subprocess.run(
                [str(args.hpfs_checker), "check", "-p", "-s", "-u", str(existing)],
                capture_output=True,
                text=True,
                check=True,
            )
            assert "total errors: 0" in (check.stdout + check.stderr).lower(), (
                check.stdout + check.stderr
            )
    if args.exfat_formatter:
        existing = work / "external-exfat.img"
        with existing.open("wb") as output:
            output.truncate(32 * 1024 * 1024)
        subprocess.run(
            [str(args.exfat_formatter), "-n", "REFERENCE", str(existing)],
            check=True,
            capture_output=True,
        )
        command("map-image", "F", str(existing))
        command("media-readonly", "F", "off")
        request("import-files", path="F:\\", sources=[str(reference)])
        if args.exfat_checker:
            subprocess.run(
                [str(args.exfat_checker), "-n", str(existing)], check=True, capture_output=True
            )
    (work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(
        "FAT32, exFAT and HPFS creation, existing-volume access, MBR partitions, R/W and R/O checks passed."
    )


if __name__ == "__main__":
    main()
