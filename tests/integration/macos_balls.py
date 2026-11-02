#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Launch generated OS/2 guests using only a relocated Mac app's bundled runtime."""

import argparse
import json
import os
import shutil
import struct
import subprocess
from pathlib import Path

from images import FlatGuest, linear_image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument(
        "--work", type=Path, required=True, help="new directory for retained evidence"
    )
    parser.add_argument(
        "--skip-copy", action="store_true", help="test in place instead of relocation"
    )
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=False)
    app = args.app.resolve()
    if not args.skip_copy:
        app = work / "Relocated App With Spaces.app"
        shutil.copytree(args.app, app, symlinks=True)
    resources = app / "Contents/Resources"
    build = resources / "Runtime"
    helper = build / "2ine-ball"
    assert not (resources / "Python").exists()
    assert not list(app.rglob("*.py"))
    ball = work / "Smoke Test.2ineball"
    environment = {
        key: value
        for key, value in os.environ.items()
        if not key.startswith(("TWOINE_", "DYLD_", "LD_", "PYTHON"))
    }
    environment["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"

    def command(arguments, expected=0):
        launch = arguments[0] in ("run", "install", "launch")
        message = {
            "action": "prepare-launch" if launch else "command",
            "ball": str(ball),
            "build": str(build),
            "arguments": arguments,
        }
        result = subprocess.run(
            [str(helper), *(["--execute"] if launch else [])],
            input=json.dumps(message).encode(),
            env=environment,
            capture_output=True,
            timeout=90,
        )
        with (work / "launch.log").open("ab") as log:
            log.write(result.stdout + result.stderr)
        assert result.returncode == expected, (
            result.returncode,
            result.stderr.decode(errors="replace"),
        )

    def request(action, **fields):
        result = subprocess.run(
            [str(helper)],
            input=json.dumps({"action": action, "ball": str(ball), **fields}).encode(),
            env=environment,
            capture_output=True,
            check=True,
            timeout=90,
        )
        return json.loads(result.stdout)

    command(["init"])
    guest = FlatGuest()
    guest.exit(0)
    executable = ball / "drive_c/OS2/smoke.exe"
    executable.write_bytes(
        linear_image(guest.code, guest.fixups, ["DOSCALLS"], "SMOKE", data=guest.data)
    )
    command(["install", "--cwd", "C:\\TEMP", "C:\\OS2\\smoke.exe", "a b", "", "C:\\DATA\\file"])
    receipt = json.loads((ball / "logs/last-install.json").read_text())
    assert receipt["exit_code"] == 0 and len(receipt["runtime_id"]) == 64
    assert receipt["command"][-3:] == ["a b", "", "C:\\DATA\\file"]
    assert receipt["cwd"] == str(ball / "drive_c/TEMP")
    command(["add-app", "--cwd", "C:\\TEMP", "Smoke", "C:\\OS2\\smoke.exe"])
    command(["launch", "Smoke"])
    command(["run", "C:\\missing.exe"], expected=2)
    command(["launch", "Smoke"])
    # The guest must see every inherited/overridden drive directory and the
    # expanded guest PATH, including tails beyond the old 254-byte limit.
    media = work / "Media"
    (media / "GAME").mkdir(parents=True)
    request("command", arguments=["map", "D", str(media)])
    request("set-working-directory", path=r"D:\GAME")
    request("set-working-directory", path=r"C:\OS2")
    probe = FlatGuest()
    for disk, expected in ((3, b"TEMP"), (4, b"GAME")):
        output = probe.field(bytes(256))
        length = probe.field(struct.pack("<I", 256))
        probe.call(1, 274, disk, output, length)
        probe.result(0, 50 + disk)
        probe.word(output, int.from_bytes(expected, "little"), 60 + disk)
    name = probe.field(b"PATH\0")
    pointer = probe.field(bytes(4))
    inherited_path = r"C:\OS2;C:\OS2\SYSTEM;" + r"C:\LONGPATH;" * 40
    request("command", arguments=["config", "env.PATH", inherited_path])
    expected_path = (inherited_path + r";C:\DOOM").encode()
    probe.call(1, 227, name, pointer)
    probe.result(0, 70)
    probe.load(pointer)
    probe.code.extend(b"\x05" + struct.pack("<I", len(expected_path) - 4))
    probe.require(b"\x81\x38" + expected_path[-4:], 71)
    probe.exit(0)
    (ball / "drive_c/OS2/defaults.exe").write_bytes(
        linear_image(probe.code, probe.fixups, ["DOSCALLS"], "DEFAULTS", data=probe.data)
    )
    request(
        "save-application",
        name="Defaults",
        program=r"C:\OS2\defaults.exe",
        arguments=[],
        workingDirectories={"C": r"C:\TEMP"},
        environment={"PATH": r"%PATH%;C:\DOOM"},
    )
    command(["launch", "Defaults"])
    assert request("snapshot")["workingDirectories"]["C"] == r"C:\OS2"
    profile = ball / "drive_c/OS2/OS2.INI"
    profile.write_bytes(b"retained-profile")
    command(["remove-app", "Smoke"])
    assert profile.read_bytes() == b"retained-profile" and executable.is_file()
    print(
        f"Python-free relocated bundle: installer receipt, cwd, argv, saved launch, drive defaults, environment expansion, recovery and persistence passed: {work}"
    )


if __name__ == "__main__":
    main()
