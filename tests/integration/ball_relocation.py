# Public domain. No rights reserved. Wholly AI-authored.
"""Run a generated LX program after moving a ball away from its original path."""

import argparse
import json
import os
import struct
import subprocess
import tempfile
from pathlib import Path

from images import FlatGuest, linear_image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    work_root = args.work.resolve()
    work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="run-", dir=work_root))
    original = work / "Original ball.2ineball"
    moved = work / "Moved folder" / "Relocated café.2ineball"
    if original.exists() or moved.exists():
        raise ValueError("Use a new work directory; an existing ball will not be overwritten")
    helper = build / ("2ine-ball.exe" if os.name == "nt" else "2ine-ball")
    ball = original
    environment = {
        k: v for k, v in os.environ.items() if not k.startswith(("TWOINE_", "DYLD_", "LD_"))
    }
    environment.update(
        SDL_VIDEO_DRIVER="dummy", SDL_AUDIO_DRIVER="dummy", SDL_RENDER_DRIVER="software"
    )

    def request(action, execute=False, expected=0, **fields):
        result = subprocess.run(
            [str(helper), *(["--execute"] if execute else [])],
            input=json.dumps({"action": action, "ball": str(ball), **fields}).encode(),
            env=environment,
            capture_output=True,
            timeout=30,
        )
        with (work / "launch.log").open("ab") as log:
            log.write(result.stdout + result.stderr)
        assert result.returncode == expected, (
            result.returncode,
            result.stderr.decode(errors="replace"),
        )
        return json.loads(result.stdout) if not execute else None

    request("command", arguments=["init"])
    g = FlatGuest()
    disk, drives = g.field(bytes(4)), g.field(bytes(4))
    directory, length = g.field(bytes(128)), g.field(struct.pack("<I", 128))
    g.call(1, 275, disk, drives)
    g.result(0, 10)
    g.word(disk, 3, 11)
    g.word(drives, 1 << 2, 12)
    g.call(1, 274, 3, directory, length)
    g.result(0, 13)
    g.word(directory, int.from_bytes(b"TEMP", "little"), 14)
    g.exit(42)
    (ball / "drive_c/MOVED.EXE").write_bytes(
        linear_image(g.code, g.fixups, ["DOSCALLS"], "MOVED", data=g.data)
    )
    request("command", arguments=["add-app", "--cwd", "C:\\TEMP", "Moved", "C:\\MOVED.EXE"])
    data = json.loads((ball / "ball.json").read_text())
    data["runtime"]["build"] = str(work / "Other computer/missing-runtime")
    data["runtime"]["backend"] = "native"
    data["future"] = {"kept": "portable application state"}
    (ball / "ball.json").write_text(json.dumps(data, indent=2) + "\n")
    profile = ball / "drive_c/OS2/OS2.INI"
    profile.write_bytes(b"original installed profile\0\xff")
    protected = {
        name: (ball / name).read_bytes()
        for name in ("ball.json", "drive_c/CONFIG.SYS", "drive_c/OS2/OS2.INI", "drive_c/MOVED.EXE")
    }
    old_cache = Path("drive_c/OS2/DLL/2INE/default/old-machine/lx_loader")
    (ball / old_cache).parent.mkdir(parents=True)
    (ball / old_cache).write_bytes(b"foreign cached runtime")
    renamed = request("rename-ball", text="Renamed café.2ineball")
    ball = Path(renamed["path"])
    assert ball == work / "Renamed café.2ineball" and not original.exists()
    for name, content in protected.items():
        assert (ball / name).read_bytes() == content, name
    moved.parent.mkdir(parents=True)
    ball.rename(moved)
    ball = moved
    selected = request("effective-runtime", build=str(build), path=data["runtime"]["build"])
    assert selected == str(build)
    plan = request("prepare-launch", build=selected, arguments=["launch", "Moved"])
    assert Path(plan["command"][0]).is_relative_to(moved)
    assert plan["environment"]["TWOINE_RUNTIME_ID"][:16] in plan["command"][0]
    assert plan["environment"]["TWOINE_BACKEND"] == args.backend
    assert str(original) not in json.dumps(plan)
    request(
        "prepare-launch",
        execute=True,
        expected=42,
        build=selected,
        arguments=["launch", "Moved"],
    )
    assert not original.exists()
    for name, content in protected.items():
        assert (ball / name).read_bytes() == content, name
    assert (ball / old_cache).read_bytes() == b"foreign cached runtime"
    print(
        "Moved ball executed its LX application with receiving-host runtime; saved state and foreign cache preserved"
    )


if __name__ == "__main__":
    main()
