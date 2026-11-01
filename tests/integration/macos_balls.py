#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Launch generated OS/2 guests using only a relocated Mac app's bundled runtime."""

import argparse
import json
import os
import shutil
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
    profile = ball / "drive_c/OS2/OS2.INI"
    profile.write_bytes(b"retained-profile")
    command(["remove-app", "Smoke"])
    assert profile.read_bytes() == b"retained-profile" and executable.is_file()
    print(
        f"Python-free relocated bundle: installer receipt, cwd, argv, saved launch, recovery and persistence passed: {work}"
    )


if __name__ == "__main__":
    main()
