#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Check internal image drives against fixtures and separately supplied installation CDs."""

import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from tests.integration.harness.disk_images import PAYLOAD, fat_image, iso_image
from tools.ball import engine as ball_api


def image_hash(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def request(ball, action, **fields):
    return ball_api.native_request({"action": action, "ball": str(ball), **fields})


def walk(ball, directory="D:\\"):
    result = {}
    pending = [directory]
    while pending:
        current = pending.pop()
        entries = request(ball, "browse", path=current)["entries"]
        for entry in entries:
            relative = entry["path"][3:].replace("\\", "/")
            result[relative.casefold()] = entry
            if entry["directory"]:
                pending.append(entry["path"])
    return result


def check_member(program, entry, reference, label=None, filesystem=None):
    command = [str(program), entry["hostPath"], str(reference)]
    if label is not None:
        command += [label, filesystem]
    subprocess.run(command, check=True)
    return hashlib.sha256(reference.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--media", action="append", type=Path, default=[])
    parser.add_argument("--floppy", action="append", type=Path, default=[])
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    ball = work / "Images.2ineball"
    request(ball, "command", arguments=["init"])
    reference = work / "member.bin"
    reference.write_bytes(PAYLOAD)
    results = []
    for name, data in [
        ("fixture.iso", iso_image()),
        ("joliet.iso", iso_image(joliet=True)),
        ("fat12.img", fat_image()),
        ("fat16.img", fat_image(16)),
    ]:
        image = work / name
        image.write_bytes(data)
        before = hashlib.sha256(data).hexdigest()
        request(ball, "command", arguments=["unmap", "D"])
        request(ball, "command", arguments=["map-image", "D", str(image)])
        if name.endswith(".img"):
            request(ball, "command", arguments=["media-readonly", "D", "on"])
        entry = walk(ball)["install/setup.exe"]
        assert request(ball, "snapshot")["manifest"]["drives"]["D"]["image"] == str(image)
        try:
            request(ball, "browse", path="D:\\..\\outside")
        except ValueError:
            pass
        else:
            raise AssertionError("image path escaped its root")
        build = work / "runtime-plan"
        build.mkdir(exist_ok=True)
        (build / "lx_loader").write_bytes(b"runtime fixture")
        plan = request(
            ball,
            "prepare-launch",
            build=str(build),
            arguments=["install", "D:\\INSTALL\\SETUP.EXE"],
        )
        assert plan["command"][1] == "D:\\INSTALL\\SETUP.EXE"
        assert "/@2ine-" not in plan["cwd"]
        assert plan["environment"]["TWOINE_DRIVE_CWD_D"] == "INSTALL"
        assert plan["environment"]["TWOINE_CURRENT_DRIVE"] == "D"
        digest = check_member(
            args.program, entry, reference, "TEST_MEDIA", "FAT" if name.endswith(".img") else "CDFS"
        )
        try:
            request(ball, "import-files", path="D:\\", sources=[str(reference)])
        except ValueError as error:
            assert "read-only" in str(error), str(error)
        else:
            raise AssertionError("image accepted an import")
        assert hashlib.sha256(image.read_bytes()).hexdigest() == before
        results.append({"image": name, "files": 1, "member_sha256": digest})
    # A running adapter must see eject and replacement without reloading drive roots.
    request(ball, "command", arguments=["unmap", "D"])
    request(ball, "command", arguments=["device", "D", "cdrom"])
    snapshot = request(ball, "snapshot")
    root = snapshot["roots"]["D"]
    assert snapshot["manifest"]["drives"]["D"] == {"device": "cdrom"}
    request(ball, "command", arguments=["map-image", "D", str(work / "fixture.iso")])
    assert request(ball, "snapshot")["roots"]["D"] == root
    first = walk(ball)["install/setup.exe"]["hostPath"]
    other = bytearray(iso_image())
    other[22 * 2048] ^= 0xFF
    replacement = work / "replacement.iso"
    replacement.write_bytes(other)
    subprocess.run(
        [str(args.program), "--swap", first, str(ball / "devices/D.media"), str(replacement)],
        check=True,
    )
    request(ball, "command", arguments=["eject", "D"])
    assert request(ball, "snapshot")["manifest"]["drives"]["D"] == {"device": "cdrom"}
    request(ball, "command", arguments=["unmap", "D"])
    # Corrupt cluster chains must fail instead of repeating data or looping.
    damaged = bytearray(fat_image())
    for origin in (512, 512 + 9 * 512):
        old = struct.unpack_from("<H", damaged, origin + 4)[0]
        struct.pack_into("<H", damaged, origin + 4, (old & 15) | (3 << 4))
    broken_fat = work / "cycle.img"
    broken_fat.write_bytes(damaged)
    request(ball, "command", arguments=["map-image", "D", str(broken_fat)])
    member = walk(ball)["install/setup.exe"]
    rejected = subprocess.run(
        [str(args.program), member["hostPath"], str(reference)], capture_output=True
    )
    assert rejected.returncode != 0, "cyclic FAT chain was accepted"
    request(ball, "command", arguments=["map-image", "D", str(work / "fat16.img")])
    # A source disappearing must not prevent opening the ball or unmapping it.
    (work / "fat16.img").unlink()
    request(ball, "snapshot")
    request(ball, "command", arguments=["unmap", "D"])
    broken = work / "truncated.iso"
    broken.write_bytes(iso_image()[: 18 * 2048])
    try:
        request(ball, "command", arguments=["map-image", "D", str(broken)])
    except ValueError:
        pass
    else:
        raise AssertionError("truncated image was accepted")
    request(ball, "command", arguments=["unmap", "D"])
    for image in args.media:
        original_hash = image_hash(image)
        request(ball, "command", arguments=["map-image", "D", str(image.resolve())])
        entries = walk(ball)
        # libarchive provides an independent ISO directory and member-byte reference.
        listing = subprocess.run(
            ["/usr/bin/bsdtar", "-tf", str(image)], check=True, capture_output=True, text=True
        ).stdout.splitlines()
        expected = {
            name.removeprefix("./").rstrip("/").casefold()
            for name in listing
            if name not in (".", "./")
        }
        assert set(entries) == expected, (image.name, sorted(set(entries) ^ expected)[:20])
        candidates = [entry for entry in entries.values() if not entry["directory"]]
        chosen = []
        for suffix in (
            "install.cmd",
            "setup.exe",
            "install.exe",
            "os2krnl",
            "readme.txt",
            ".dll",
            ".img",
            ".zip",
        ):
            found = next(
                (
                    entry
                    for entry in candidates
                    if entry["name"].lower().endswith(suffix) and entry not in chosen
                ),
                None,
            )
            if found:
                chosen.append(found)
        if not chosen:
            chosen = candidates[:3]
        hashes = {}
        with image.open("rb") as stream:
            stream.seek(16 * 2048 + 40)
            label = stream.read(32).decode("ascii").rstrip()[:11]
        reference_names = {name.removeprefix("./").rstrip("/").casefold(): name for name in listing}
        for entry in chosen:
            relative = entry["path"][3:].replace("\\", "/")
            with reference.open("wb") as output:
                subprocess.run(
                    ["/usr/bin/bsdtar", "-xOf", str(image), reference_names[relative.casefold()]],
                    check=True,
                    stdout=output,
                )
            hashes[relative] = check_member(args.program, entry, reference, label, "CDFS")
        assert image_hash(image) == original_hash
        results.append(
            {
                "source_sha256": original_hash,
                "image": image.name,
                "entries": len(entries),
                "label": label,
                "filesystem": "CDFS",
                "members": hashes,
            }
        )
        print(
            f"{image.name}: {len(entries)} directory entries agree; {len(hashes)} member byte checks pass",
            flush=True,
        )
    for image in args.floppy:
        original_hash = image_hash(image)
        request(ball, "command", arguments=["map-image", "A", str(image.resolve())])
        request(ball, "command", arguments=["media-readonly", "A", "on"])
        entries = walk(ball, "A:\\")
        sevenzip = shutil.which("7zz") or shutil.which("7z")
        if sevenzip is None:
            raise RuntimeError("7-Zip is required for independent floppy-media checks")
        hashes = {}
        for filename in ("CONFIG.SYS", "OS2LDR", "OS2BOOT"):
            entry = entries.get(filename.casefold())
            if entry is None:
                continue
            with reference.open("wb") as output:
                subprocess.run(
                    [sevenzip, "x", "-so", str(image), filename], check=True, stdout=output
                )
            hashes[filename] = check_member(args.program, entry, reference)
        assert hashes, "No reference members found on floppy"
        request(ball, "command", arguments=["unmap", "A"])
        assert image_hash(image) == original_hash
        results.append(
            {"image": image.name, "entries": len(entries), "filesystem": "FAT", "members": hashes}
        )
        print(
            f"{image.name}: {len(entries)} entries read; {len(hashes)} member byte checks pass",
            flush=True,
        )
    request(ball, "command", arguments=["unmap", "D"])
    (work / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    print(f"Internal image-drive checks passed for {len(results)} images.")


if __name__ == "__main__":
    main()
