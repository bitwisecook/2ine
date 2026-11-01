#!/usr/bin/env python3
# Public domain. No rights reserved.
"""Verify native OS/2 GLQuake with shareware data and real X11 input."""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time
from pathlib import Path

from linux.x11 import Display, move_pointer, position, t, x


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "program", "data", "work"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    parser.add_argument("--gl10", action="store_true")
    parser.add_argument("--mouse", action="store_true")
    args = parser.parse_args()
    build, work, program = args.build.resolve(), args.work.resolve(), args.program.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / "id1").mkdir(exist_ok=True)
    shutil.copyfile(args.data, work / "id1/pak0.pak")
    env = dict(
        os.environ,
        TWOINE_BACKEND=args.backend,
        TWOINE_LIBPATH=str(build),
        LD_LIBRARY_PATH=str(build),
        SDL_VIDEO_DRIVER="x11",
        SDL_RENDER_DRIVER="software",
        LIBGL_ALWAYS_SOFTWARE="1",
    )
    command = [
        str(build / "lx_loader"),
        str(program),
        "-width",
        "320",
        "-height",
        "240",
        "-nomouse",
        "-inputlog",
        "+developer",
        "1",
        "+map",
        "e1m1",
        "+bind",
        "F11",
        "screenshot",
        "+bind",
        "F12",
        "quit",
    ]
    if args.mouse:
        command.remove("-nomouse")
        command.append("+mlook")
    if args.gl10:
        command.append("-gl10")
    with (work / "runtime.log").open("wb") as log:
        process = subprocess.Popen(command, cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            display = Display(os.environ["DISPLAY"])
            deadline = time.monotonic() + 150
            while time.monotonic() < deadline:
                contents = (work / "runtime.log").read_bytes()
                if b"CL_SignonReply: 4" in contents:
                    break
                if process.poll() is not None:
                    raise RuntimeError("GLQuake exited before entering the map")
                time.sleep(0.2)
            else:
                raise RuntimeError("GLQuake did not enter E1M1 before timeout")
            window = next(w for w, name in display.windows() if name == "GLQuake for OS/2")
            display.focus(window)
            time.sleep(0.5)
            before = display.capture(window, work / "map.png")
            display.key(0xFF1B)
            time.sleep(1)
            menu = display.capture(window, work / "menu.png")
            display.key(0xFF1B)
            time.sleep(0.5)
            key = x.XKeysymToKeycode(display.d, 0xFF52)
            t.XTestFakeKeyEvent(display.d, key, 1, 0)
            x.XFlush(display.d)
            time.sleep(1)
            t.XTestFakeKeyEvent(display.d, key, 0, 0)
            x.XFlush(display.d)
            time.sleep(0.5)
            moved = display.capture(window, work / "moved.png")
            if args.mouse:
                px, py = position(display, window)
                move_pointer(display, px + 160, py + 120)
                time.sleep(0.3)
                move_pointer(display, px + 184, py + 128)
                time.sleep(0.5)
                display.capture(window, work / "mouse.png")
                display.click(window, 160, 120, 1)
                time.sleep(0.3)
            display.key(0xFFC8)
            time.sleep(1)
            display.key(0xFFC9)
            time.sleep(1)
            display.key(ord("y"))
            rc = process.wait(timeout=30)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    contents = (work / "runtime.log").read_text(errors="replace")
    colors = len({before[i : i + 3] for i in range(0, len(before), 3)})
    result = {
        "command": command,
        "exit": rc,
        "colors": colors,
        "menu_changed": before != menu,
        "movement_changed": before != moved,
        "program_sha256": hashlib.sha256(program.read_bytes()).hexdigest(),
    }
    (work / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    assert rc == 0 and colors > 100 and before != menu and before != moved
    for fragment in (
        "PM key=27 down=1 dest=3",
        "PM key=27 down=1 dest=0",
        "PM key=128 down=1",
        "PM key=128 down=0",
        "Wrote quake00.tga",
        "GLQuake shutdown complete",
    ):
        assert fragment in contents, fragment
    if args.mouse:
        assert "PM mouse dx=" in contents and "PM button=200" in contents
    screenshot = (work / "id1/quake00.tga").read_bytes()
    assert len(screenshot) == 320 * 240 * 3 + 18
    assert len(set(screenshot[18:])) > 100
    print("GLQuake E1M1 rendering, menu, movement, screenshot and clean exit verified")


if __name__ == "__main__":
    main()
