#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise Word Pro document editing with separately supplied installed media/settings."""

import argparse
import hashlib
import json
import os
import select
import shutil
import subprocess
import time
from pathlib import Path

from linux.x11 import Display, x


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args, display, env):
    work = args.work.resolve()
    document = work / "document.lwp"
    document.unlink(missing_ok=True)
    guest_name = "C:" + str(document).replace("/", "\\")
    if len(guest_name.encode()) >= 260:
        raise ValueError("document path exceeds the OS/2 file-dialog limit")
    snapshots = []
    stages = []
    with (work / "wordpro.log").open("wb") as log:
        process = subprocess.Popen(
            [str(args.build.resolve() / "lx_loader"), "WORDPRO.EXE"],
            cwd=args.installation.resolve() / "WORDPRO",
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
        )

        def pause(seconds):
            time.sleep(seconds)
            if process.poll() is not None:
                raise RuntimeError(f"Word Pro exited early: {process.returncode}; see {log.name}")

        def window(title):
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                for handle, name in display.windows():
                    if name == title:
                        try:
                            display.geometry(handle)
                            return handle
                        except OSError:
                            pass
                pause(0.05)
            raise RuntimeError(f"missing {title!r} window; see {log.name}")

        def capture(handle, name):
            path = work / f"{name}.png"
            display.capture(handle, path)
            snapshots.append(path.name)

        def menu(main, mnemonic):
            display.click(main, 25, 12, 1)
            pause(0.2)
            display.key(ord(mnemonic))
            if mnemonic != "x":
                pause(0.7)

        def filename(handle):
            display.focus(handle)
            display.click(handle, 90, 26, 1)
            display.key(ord("a"), [0xFFE3])
            pause(0.1)
            display.text(guest_name)
            pause(0.2)
            display.key(0xFF0D)
            pause(2)

        def font(main, index):
            height = display.geometry(main)[1]
            display.click(main, 50, height - 9, 1)
            pause(0.5)
            capture(main, f"font-selector-{index}")
            display.click(main, 30, height - 56 + index * 14, 1)
            pause(0.5)

        def reopen(main, name):
            menu(main, "c")
            capture(main, f"closed-{name}")
            menu(main, "o")
            filename(window("Open"))
            # Xvfb has no window manager to restore native focus after a modal closes.
            display.focus(main)
            pause(0.2)
            capture(main, name)

        try:
            welcome = window("Welcome to Lotus Word Pro")
            pause(0.5)
            width, height = display.geometry(welcome)
            capture(welcome, "welcome")
            display.click(welcome, width * 110 // 335, height - height * 8 // 164, 1)
            main = window("Word Pro")
            pause(2)
            display.focus(main)
            capture(main, "blank-document")
            display.text("word pro editing test")
            pause(0.5)
            capture(main, "new-document")
            font(main, 0)  # Courier, selected in the genuine three-family popup.
            display.text(" courier text")
            pause(0.5)
            menu(main, "s")
            save_as = window("Save As")
            capture(save_as, "save-as")
            filename(save_as)
            pause(1)
            capture(main, "saved")
            first = document.read_bytes()
            assert first.startswith(b"WordPro")
            assert all(
                value in first for value in (b"word pro editing test", b"courier text", b"Courier")
            )
            stages.append({"stage": "created", "bytes": len(first), "sha256": digest(document)})
            reopen(main, "reopened")
            display.key(0xFF57, [0xFFE3])
            display.key(0xFF0D)
            pause(0.2)
            font(main, 1)  # Helv for the new paragraph after reopening.
            display.text("edited after reopen")
            pause(0.5)
            capture(main, "edited-after-reopen")
            menu(main, "s")
            pause(1)
            final = document.read_bytes()
            assert final != first
            assert all(value in final for value in (b"edited after reopen", b"Courier", b"Helv"))
            stages.append({"stage": "edited", "bytes": len(final), "sha256": digest(document)})
            reopen(main, "final-reopened")
            assert document.read_bytes() == final
            menu(main, "x")
            result = process.wait(timeout=args.timeout)
            assert result == 0, f"normal exit failed: {result}"
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    receipt = {
        "backend": args.backend,
        "executable_sha256": digest(args.installation / "WORDPRO" / "WORDPRO.EXE"),
        "document": str(document),
        "stages": stages,
        "snapshots": snapshots,
        "screenshot_sha256": {name: digest(work / name) for name in snapshots},
        "worker_sha256": digest(args.build / "lx_loader"),
        "runtime_sha256": digest(args.build / "lib2ine.so"),
        "fonts": "warp" if env.get("TWOINE_WARP_FONT_DIRECTORY") else "liberation",
        "normal_exit": result,
    }
    (work / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    parser.add_argument("--installation", type=Path, required=True)
    parser.add_argument("--settings", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=45)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    (args.work / "receipt.json").unlink(missing_ok=True)
    build, work, installation = (
        args.build.resolve(),
        args.work.resolve(),
        args.installation.resolve(),
    )
    env = dict(
        os.environ,
        TWOINE_BACKEND=args.backend,
        LD_LIBRARY_PATH=str(build),
        TWOINE_LIBPATH=":".join(
            map(str, (build, installation / "WORDPRO", installation / "COMPNENT"))
        ),
        SDL_VIDEO_DRIVER="x11",
        SDL_RENDER_DRIVER="software",
        TRACE_NATIVE="1",
        TRACE_EVENTS="1",
    )
    for key, name in (
        ("TWOINE_USER_PROFILE", "user.profile"),
        ("TWOINE_SYSTEM_PROFILE", "system.profile"),
        ("TWOINE_REGISTRY", "registry.profile"),
    ):
        destination = work / name
        source = args.settings / name
        if source.is_file():
            shutil.copyfile(source, destination)
        elif name == "system.profile":
            destination.unlink(missing_ok=True)
        else:
            raise FileNotFoundError(source)
        env[key] = str(destination)
    read, write = os.pipe()
    server = subprocess.Popen(
        ["Xvfb", "-displayfd", str(write), "-screen", "0", "1024x768x24", "-nolisten", "tcp"],
        pass_fds=(write,),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    os.close(write)
    display = None
    try:
        if not select.select([read], [], [], 10)[0]:
            raise RuntimeError("Xvfb did not start")
        env["DISPLAY"] = ":" + os.read(read, 32).decode().strip()
        display = Display(env["DISPLAY"])
        run(args, display, env)
    finally:
        os.close(read)
        if display:
            x.XCloseDisplay(display.d)
        server.terminate()
        server.wait(timeout=3)


if __name__ == "__main__":
    main()
