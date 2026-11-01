#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Check host fullscreen pixels, mouse mapping, hotkey consumption and restoration."""

import argparse
import json
import os
import select
import subprocess
import time
from pathlib import Path

from linux.fullscreen import toggle_fullscreen, wait_for
from linux.wm import WindowManager
from linux.x11 import Display, check_errors, x


def check_colors(rgb, size):
    width, height = size
    for px, color in ((width // 4, bytes((224, 32, 64))), (3 * width // 4, bytes((32, 192, 96)))):
        offset = ((height // 2) * width + px) * 3
        assert rgb[offset : offset + 3] == color, (size, px, rgb[offset : offset + 3])


def wait_for_colors(display, window, process, path):
    def painted():
        rgb = display.capture(window, path)
        try:
            check_colors(rgb, display.geometry(window))
        except AssertionError:
            return None
        return rgb

    return wait_for(painted, process)


def run_case(args, display, display_name, startup):
    work = args.work / ("startup" if startup else "shortcut")
    work.mkdir(parents=True, exist_ok=True)
    log = work / "worker.log"
    with log.open("w") as output:
        process = subprocess.Popen(
            [str(args.program)],
            env={
                **os.environ,
                "DISPLAY": display_name,
                "SDL_VIDEO_DRIVER": "x11",
                "SDL_RENDER_DRIVER": "software",
                "TWOINE_FULLSCREEN": str(int(startup)),
            },
            stdout=output,
            stderr=output,
        )
        try:

            def find_window():
                return next(
                    (
                        handle
                        for handle, title in display.windows()
                        if title == "2ine fullscreen contract"
                    ),
                    None,
                )

            wait_for(lambda: "fullscreen " in log.read_text(), process)
            window = wait_for(find_window, process)
            desktop = display.geometry(display.root)
            wait_for(
                lambda: display.geometry(window) == (desktop if startup else (160, 120)), process
            )
            display.focus(window)
            if not startup:
                initial = wait_for_colors(display, window, process, work / "initial.png")
                wait_for(lambda: display.position(window) == (37, 53), process)
                display.key(0xFFC8)  # F11 must still reach the guest.
                toggle_fullscreen(display, window, process, desktop)
            wait_for_colors(display, window, process, work / "fullscreen.png")
            assert display.position(window) == (0, 0)
            display.move_pointer(desktop[0] * 3 // 4, desktop[1] * 3 // 4)
            display.key(ord("p"))
            wait_for(lambda: "pointer 157 143" in log.read_text(), process)
            display.click(window, desktop[0] * 3 // 4, desktop[1] * 3 // 4, 1)
            wait_for(lambda: "click 120 90" in log.read_text(), process)
            display.key(ord("w"))
            time.sleep(0.1)
            display.key(ord("p"))
            wait_for(lambda: "pointer 77 83" in log.read_text(), process)
            toggle_fullscreen(display, window, process, (160, 120))
            wait_for(lambda: display.position(window) == (37, 53), process)
            restored = wait_for_colors(display, window, process, work / "restored.png")
            if not startup:
                assert restored == initial, (
                    "restoring fullscreen changed guest render-target pixels"
                )
            display.key(0xFFC8)
            display.key(ord("q"))
            assert process.wait(timeout=5) == 0, log.read_text()
            contents = log.read_text()
            assert contents.count("F11\n") == (1 if startup else 2), contents
            assert "fullscreen 1\n" in contents and "fullscreen 0\n" in contents, contents
            assert "fullscreen contract passed" in contents, contents
            return dict(
                startup=startup, desktop=desktop, restored=(160, 120), pixels_preserved=True
            )
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.program, args.work = args.program.resolve(), args.work.resolve()
    args.work.mkdir(parents=True, exist_ok=True)
    receipt = args.work / "result.json"
    receipt.unlink(missing_ok=True)
    read_fd, write_fd = os.pipe()
    display = None
    manager = None
    with (args.work / "xvfb.log").open("w") as server_log:
        server = subprocess.Popen(
            [
                "Xvfb",
                "-displayfd",
                str(write_fd),
                "-screen",
                "0",
                "1024x768x24",
                "-nolisten",
                "tcp",
            ],
            pass_fds=(write_fd,),
            stdout=server_log,
            stderr=server_log,
        )
        os.close(write_fd)
        try:
            assert select.select([read_fd], [], [], 10)[0], "Xvfb did not allocate a display"
            name = ":" + os.read(read_fd, 32).decode().strip()
            manager = WindowManager(name)
            display = Display(name)
            cases = [run_case(args, display, name, startup) for startup in (False, True)]
            check_errors()
            receipt.write_text(json.dumps(dict(cases=cases), indent=2) + "\n")
            print("Host fullscreen pixels, pointer mapping, Alt+Enter, F11 and restoration passed")
        finally:
            os.close(read_fd)
            if display:
                x.XCloseDisplay(display.d)
            if manager:
                manager.close()
            server.terminate()
            server.wait(timeout=5)


if __name__ == "__main__":
    main()
