#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Execute an original-format LX installer from an internal CD device into private C:."""

import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from disk_images import iso_image
from images import FlatGuest, linear_image

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import ball as ball_api


def installer():
    guest = FlatGuest()
    drive, bitmap = (guest.field(bytes(4)) for _ in range(2))
    output = guest.field(bytes(128))
    length = guest.field(struct.pack("<I", 128))
    guest.call(1, 275, drive, bitmap)
    guest.result(0, 10)
    guest.word(drive, 4, 11)
    guest.word(bitmap, (1 << 0) | (1 << 2) | (1 << 3), 12)
    guest.call(1, 274, 0, output, length)
    guest.result(0, 13)
    guest.word(output, int.from_bytes(b"INST", "little"), 14)
    device = guest.field(b"D:\0")
    attach_length = guest.field(struct.pack("<I", 128))
    guest.call(1, 277, device, 0, 1, output, attach_length)
    guest.result(0, 15)
    guest.word(output + 11, int.from_bytes(b"CDFS", "little"), 16)
    parameters = guest.field(bytes([0, 3]))
    parameter_size, data_size = (guest.field(struct.pack("<I", 36)) for _ in range(2))
    guest.call(1, 284, 0xFFFFFFFF, 8, 0x63, parameters, 2, parameter_size, output, 128, data_size)
    guest.result(0, 17)
    guest.word(output, 0x00010800, 18)  # 2048-byte sector, one sector per cluster.
    source = guest.field(b"SETUP.EXE\0")
    destination = guest.field(b"C:\\OS2\\Installed.exe\0")
    guest.call(1, 258, source, destination, 0)
    guest.result(0, 19)
    guest.exit(0)
    return linear_image(guest.code, guest.fixups, ["DOSCALLS"], "MEDIAINST", data=guest.data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--original-cmd-media", type=Path)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    # Keep repeated runs independent without deleting an existing ball or image.
    work = Path(tempfile.mkdtemp(prefix="run-", dir=work))
    ball = work / "Installer.2ineball"
    media = work / "Installer.iso"
    payload = installer()
    media.write_bytes(iso_image(payload=payload))
    before = hashlib.sha256(media.read_bytes()).hexdigest()

    def request(action, **fields):
        return ball_api.native_request({"action": action, "ball": str(ball), **fields})

    request("command", arguments=["init"])
    request("command", arguments=["device", "A", "floppy"])
    request("command", arguments=["device", "D", "cdrom"])
    request("command", arguments=["map-image", "D", str(media)])
    plan = request(
        "prepare-launch",
        build=str(args.build.resolve()),
        arguments=["install", "D:\\INSTALL\\SETUP.EXE"],
    )
    env = {key: value for key, value in os.environ.items() if key not in plan["unsetEnvironment"]}
    env.update(plan["environment"], SDL_VIDEO_DRIVER="dummy", SDL_RENDER_DRIVER="software")
    result = subprocess.run(
        plan["command"], cwd=plan["cwd"], env=env, capture_output=True, timeout=20
    )
    assert result.returncode == 0, (result.returncode, result.stderr.decode(errors="replace"))
    assert (ball / "drive_c/OS2/Installed.exe").read_bytes() == payload
    request("command", arguments=["eject", "D"])
    request(
        "record-install",
        receipt=plan["receipt"],
        programHost=plan["programHost"],
        exitCode=result.returncode,
    )
    receipt = json.loads((ball / "logs/last-install.json").read_text())
    assert receipt["program_sha256"] == hashlib.sha256(payload).hexdigest()
    assert request("snapshot")["manifest"]["drives"]["D"] == {"device": "cdrom"}
    assert hashlib.sha256(media.read_bytes()).hexdigest() == before
    print(
        "CD installer executed, copied itself to private C:, recorded its result and ejected media without removing D:."
    )

    if args.original_cmd_media:
        original = args.original_cmd_media.resolve()
        digest = hashlib.file_digest(original.open("rb"), "sha256").hexdigest()
        request("command", arguments=["map-image", "D", str(original)])
        script = ball / "drive_c/OS2/Media Probe.cmd"
        script.write_bytes(b"@echo off\r\necho Original OS/2 command processor\r\nexit 0\r\n")
        plan = request(
            "prepare-launch",
            build=str(args.build.resolve()),
            arguments=["install", "C:\\OS2\\Media Probe.cmd"],
        )
        env.update(plan["environment"])
        result = subprocess.run(
            plan["command"], cwd=plan["cwd"], env=env, capture_output=True, timeout=20
        )
        (work / "original-cmd-output.log").write_bytes(result.stdout + result.stderr)
        assert result.returncode == 0, (result.returncode, result.stderr.decode(errors="replace"))
        with original.open("rb") as source:
            assert hashlib.file_digest(source, "sha256").hexdigest() == digest
        print(
            "The original Warp 4.52 CMD.EXE executed the selected command file directly from the installation ISO."
        )


if __name__ == "__main__":
    main()
