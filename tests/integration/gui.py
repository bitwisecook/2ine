#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise archived PM applications with a private Xvfb, pixels and real input.

Requires Linux X11/XTest and Xvfb. Fixtures are supplied separately. PMStars is
an unchanged archived executable; RunBox can be rebuilt from its archived C
source with an OS/2 compiler. Results retain program/worker/library hashes.
"""

import argparse
import hashlib
import json
import os
import select
import subprocess
import sys
import time
from pathlib import Path

from linux.x11 import Display, check_errors, x


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def wait_for(condition, process, seconds=8):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"worker exited early: {process.returncode}")
        value = condition()
        if value:
            return value
        time.sleep(0.02)
    raise AssertionError("GUI condition timed out")


def colored_pixels(rgb):
    return sum(
        1
        for r, g, b in zip(rgb[::3], rgb[1::3], rgb[2::3], strict=True)
        if r != g or g != b or r > 0
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--program", type=Path, required=True)
    parser.add_argument("--backend", choices=["native", "unicorn"], required=True)
    parser.add_argument("--profile", choices=["pmstars", "runbox"], required=True)
    parser.add_argument("--expected-exit", type=int, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.build, args.program, args.work = (
        args.build.resolve(),
        args.program.resolve(),
        args.work.resolve(),
    )
    assert args.program.is_file()
    args.work.mkdir(parents=True, exist_ok=True)
    receipt = args.work / "result.json"
    receipt.unlink(missing_ok=True)
    read_fd, write_fd = os.pipe()
    display = None
    process = None
    with (
        (args.work / "xvfb.log").open("w") as server_log,
        (args.work / "worker.log").open("w") as worker_log,
    ):
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
            number = os.read(read_fd, 32).decode().strip()
            assert number.isdecimal(), "Xvfb startup failed"
            name = ":" + number
            display = Display(name)
            command = [str(args.build / "lx_loader"), args.program.name]
            environment = {
                **os.environ,
                "DISPLAY": name,
                "SDL_VIDEO_DRIVER": "x11",
                "SDL_RENDER_DRIVER": "software",
                "TWOINE_BACKEND": args.backend,
                "TWOINE_LIBPATH": str(args.build),
                "LD_LIBRARY_PATH": str(args.build),
                "TRACE_NATIVE": "1",
                "TRACE_EVENTS": "1",
            }
            process = subprocess.Popen(
                command,
                cwd=args.program.parent,
                env=environment,
                stdout=worker_log,
                stderr=worker_log,
            )

            def window():
                for handle, title in display.windows():
                    try:
                        size = display.geometry(handle)
                    except OSError:
                        continue
                    if args.profile == "pmstars" and title == "PMStars" and size == (500, 400):
                        return handle
                    if args.profile == "runbox" and not title and size == (50, 50):
                        return handle
                return None

            handle = wait_for(window, process)
            result = dict(
                program=str(args.program),
                program_sha256=digest(args.program),
                backend=args.backend,
                command=command,
                cwd=str(args.program.parent),
                worker_sha256=digest(args.build / "lx_loader"),
                libraries={p.name: digest(p) for p in args.build.glob("lib*.so") if p.is_file()},
                profile=args.profile,
                window_size=display.geometry(handle),
            )
            if args.profile == "pmstars":
                assert display.geometry(handle) == (500, 400)

                def painted():
                    rgb = display.capture(handle, args.work / "initial.png")
                    return rgb if colored_pixels(rgb) > 100 else None

                initial = wait_for(painted, process)
                time.sleep(0.2)
                animated = display.capture(handle, args.work / "animated.png")
                assert animated != initial, "starfield did not animate"
                display.click(handle, 120, 100, 1)

                def origin_changed():
                    clicked = display.capture(handle, args.work / "clicked.png")
                    width, _ = display.geometry(handle)
                    near = sum(
                        clicked[(y * width + px) * 3 : (y * width + px) * 3 + 3] != b"\0\0\0"
                        for y in range(85, 116)
                        for px in range(105, 136)
                    )
                    return near >= 10

                wait_for(origin_changed, process, 2)
                display.click(handle, 200, 100, 3)

                def dot_mode():
                    dots = display.capture(handle, args.work / "dots.png")
                    return 5 <= colored_pixels(dots) <= 100

                wait_for(dot_mode, process, 2)
                display.resize(handle, 600, 450)
                wait_for(lambda: display.geometry(handle) == (600, 450), process)

                def repainted():
                    resized = display.capture(handle, args.work / "resized.png")
                    return colored_pixels(resized) > 5

                wait_for(repainted, process, 2)
                result.update(
                    animation=True, left_click_origin=True, right_click_dot_mode=True, resize=True
                )
            else:
                time.sleep(0.2)
                before = display.position(handle)
                cursor = (before[0] + 25, before[1] + 25)
                display.move_pointer(*cursor)

                def moved_away():
                    position = display.position(handle)
                    return (
                        position
                        if abs(position[0] - before[0]) + abs(position[1] - before[1]) >= 10
                        else None
                    )

                after = wait_for(moved_away, process, 3)
                rgb = display.capture(handle, args.work / "moving.png")
                assert all(v == 255 for v in rgb), "default GPI background was not rendered"
                result.update(pointer_avoidance=True, position_before=before, position_after=after)
            display.close(handle)
            status = process.wait(timeout=8)
            assert status == args.expected_exit, (status, args.expected_exit)
            worker_log.flush()
            log = (args.work / "worker.log").read_text()
            assert "WM_CLOSE" in log and "WinTerminate" in log and "WM_TIMER" in log
            if args.profile == "pmstars":
                assert "WM_BUTTON1DOWN" in log and "WM_BUTTON2DOWN" in log
            assert not any(
                s in log for s in ["SIGSEGV", "missing import", "CPU stop", "Unhandled message"]
            ), "worker reported an unsupported GUI path"
            check_errors()
            result.update(
                passed=True,
                exit=status,
                clean_close=True,
                screenshots={p.name: digest(p) for p in args.work.glob("*.png")},
            )
            receipt.write_text(json.dumps(result, indent=2) + "\n")
            print(f"PASS {args.profile} {args.backend}: rendered, interacted and closed")
        except BaseException:
            worker_log.flush()
            dots = args.work / "dots.png"
            print(
                f"GUI evidence: {args.work}; dot capture present: {dots.exists()}", file=sys.stderr
            )
            print(
                "\n".join((args.work / "worker.log").read_text().splitlines()[-160:]),
                file=sys.stderr,
            )
            raise
        finally:
            os.close(read_fd)
            if process and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            if display:
                x.XCloseDisplay(display.d)
            server.terminate()
            server.wait(timeout=3)


if __name__ == "__main__":
    main()
