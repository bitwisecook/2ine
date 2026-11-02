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
import sys
import time
from pathlib import Path

from images import FlatGuest, linear_image
from linux.x11 import Display, x


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args, display, env):
    work = args.work.resolve()
    document = args.document_path if args.ball else work / "document.lwp"
    document.unlink(missing_ok=True)
    guest_name = args.document_guest if args.ball else "C:" + str(document).replace("/", "\\")
    if len(guest_name.encode()) >= 260:
        raise ValueError("document path exceeds the OS/2 file-dialog limit")
    snapshots = []
    stages = []
    with (work / "wordpro.log").open("wb") as log:
        process = subprocess.Popen(
            args.launch_command,
            cwd=args.installation.resolve() / "WORDPRO",
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
        )

        def pause(seconds):
            time.sleep(seconds * (4 if args.backend == "unicorn" else 1))
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

        def dictionary_warning():
            messages = [
                line
                for line in Path(log.name).read_text(errors="replace").splitlines()
                if "WinMessageBox(" in line
            ]
            if not messages or "Cannot find the spelling dictionary" not in messages[-1]:
                return
            caption = messages[-1].rsplit("', '", 1)[-1].split("',", 1)[0]
            warning = next(
                (handle for handle, title in display.windows() if title == caption), None
            )
            if warning:
                capture(warning, "missing-dictionary-warning")
                display.focus(warning)
                display.key(0xFF0D)
                pause(0.5)
                return True
            return False

        def menu(main, mnemonic):
            display.click(main, 37, 12, 1)
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
            pause(2)
            dictionary_warning()
            capture(main, f"font-selector-{index}")
            display.click(main, 30, height - 56 + index * 14, 1)
            pause(0.5)
            display.focus(main)

        def reopen(main, name):
            menu(main, "c")
            capture(main, f"closed-{name}")
            menu(main, "o")
            filename(window("Open"))
            warning = next(
                (handle for handle, title in display.windows() if title == "Lotus Word Pro"), None
            )
            if warning:
                messages = [
                    line
                    for line in Path(log.name).read_text(errors="replace").splitlines()
                    if "WinMessageBox(" in line
                ]
                if (
                    not messages
                    or "Your system does not have a default printer driver." not in messages[-1]
                ):
                    raise RuntimeError(f"unexpected Word Pro reopen message; see {log.name}")
                capture(warning, f"{name}-formatting-warning")
                display.focus(warning)
                display.key(0xFF0D)
                pause(0.5)
            # Xvfb has no window manager to restore native focus after a modal closes.
            display.focus(main)
            pause(0.2)
            capture(main, name)

        try:
            deadline = time.monotonic() + args.timeout
            while True:
                welcome = window("Welcome to Lotus Word Pro")
                pause(0.5)
                try:
                    width, height = display.geometry(welcome)
                    capture(welcome, "welcome")
                    break
                except (OSError, AssertionError):
                    if time.monotonic() >= deadline:
                        raise
            display.click(welcome, width * 110 // 335, height - height * 8 // 164, 1)
            for _ in range(3):
                pause(2)
                windows = list(display.windows())
                preferences = next(
                    (handle for handle, title in windows if title == "Word Pro Preferences"), None
                )
                warning = next(
                    (handle for handle, title in windows if title == "Lotus Word Pro"), None
                )
                if preferences:
                    capture(preferences, "first-run-preferences")
                    display.focus(preferences)
                    display.key(0xFF0D)
                elif warning:
                    capture(warning, "screen-formatting-warning")
                    if "Your system does not have a default printer driver." not in Path(
                        log.name
                    ).read_text(errors="replace"):
                        raise RuntimeError(f"unexpected Word Pro startup message; see {log.name}")
                    display.focus(warning)
                    display.key(0xFF0D)
                else:
                    break
            main = window("Word Pro")
            pause(2)
            display.focus(main)
            capture(main, "blank-document")
            display.text("word pro editing test")
            pause(1.5)
            dictionary_warning()
            capture(main, "new-document")
            font(main, 0)  # Courier, selected in the genuine three-family popup.
            display.text(" courier text")
            pause(1.5)
            dictionary_warning()
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
            pause(1.5)
            dictionary_warning()
            capture(main, "edited-after-reopen")
            menu(main, "s")
            pause(1)
            final = document.read_bytes()
            assert final != first
            assert all(value in final for value in (b"edited after reopen", b"Courier", b"Helv"))
            assert final.index(b"edited after reopen") > final.index(b"courier text"), (
                "reopened edit was inserted before the existing text"
            )
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
        "module_sha256": {
            path.name: digest(path) for path in sorted(args.build.glob("lib*.so")) if path.is_file()
        },
        "harness_sha256": digest(Path(__file__)),
        "initial_preferences_sha256": args.preferences_sha256,
        "ball": str(args.ball.resolve()) if args.ball else None,
        "initial_profiles_sha256": args.initial_profiles_sha256,
        "fonts": "warp" if env.get("TWOINE_WARP_FONT_DIRECTORY") else "liberation",
        "normal_exit": result,
    }
    (work / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


def prepare_preferences(args, env, build, work):
    source = args.settings / "l97user.ini"
    if not source.is_file():
        source = args.installation / "WORDPRO" / "L97USER.INI"
    if not source.is_file():
        raise FileNotFoundError("supply settings/l97user.ini or installation/WORDPRO/L97USER.INI")
    shutil.copyfile(source, work / "l97user.ini")
    (work / "lwp.ini").unlink(missing_ok=True)
    guest_path = "C:" + str(work).replace("/", "\\")
    guest = FlatGuest()
    app, key, value = (
        guest.field(text.encode() + b"\0") for text in ("WordPro", "INIPath", guest_path)
    )
    guest.call(2, 116, 0xFFFFFFFF, app, key, value)
    guest.result(1, 1)
    guest.exit(0)
    executable = work / "preferences.exe"
    executable.write_bytes(
        linear_image(guest.code, guest.fixups, ["DOSCALLS", "PMSHAPI"], "PREFS", data=guest.data)
    )
    with (work / "preferences.log").open("wb") as log:
        subprocess.run(
            [str(build / "lx_loader"), str(executable)],
            cwd=work,
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
            timeout=args.timeout,
        )
    return digest(source)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--backend", choices=("native", "unicorn"), required=True)
    parser.add_argument("--installation", type=Path)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--settings", type=Path)
    source.add_argument("--ball", type=Path, help="use the original installer-created settings")
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=45)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    (args.work / "receipt.json").unlink(missing_ok=True)
    build, work = args.build.resolve(), args.work.resolve()
    args.initial_profiles_sha256 = {}
    if args.ball:
        sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
        from tools import balls

        args.ball = args.ball.resolve()
        data = balls.load(args.ball)
        args.installation = balls.host_path(r"C:\LOTUSW4", args.ball, data)
        balls.write_config(args.ball, data)
        env = balls.environment(args.ball, data, build, args.backend)
        runtime = Path(env["TWOINE_LIBPATH"].split(os.pathsep)[0])
        balls.sync_runtime(build, runtime)
        args.launch_command = [str(runtime / "lx_loader"), r"C:\LOTUSW4\WORDPRO\WORDPRO.EXE"]
        args.document_guest = rf"C:\TEMP\wordpro-{work.name}\document.lwp"
        args.document_path = balls.host_path(args.document_guest, args.ball, data)
        args.document_path.parent.mkdir(parents=True, exist_ok=True)
        args.preferences_sha256 = None
        for key in ("TWOINE_USER_PROFILE", "TWOINE_SYSTEM_PROFILE", "TWOINE_REGISTRY"):
            path = Path(env[key])
            args.initial_profiles_sha256[key] = digest(path) if path.is_file() else None
        env.update(
            SDL_VIDEO_DRIVER="x11", SDL_RENDER_DRIVER="software", TRACE_NATIVE="1", TRACE_EVENTS="1"
        )
    else:
        if not args.installation:
            parser.error("--installation is required with --settings")
        installation = args.installation.resolve()
        env = dict(
            os.environ,
            TWOINE_BACKEND=args.backend,
            TWOINE_IPC_DIR=str(work / "ipc"),
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
        (work / "ipc").mkdir(exist_ok=True)
        args.preferences_sha256 = prepare_preferences(args, env, build, work)
        args.launch_command = [str(build / "lx_loader"), "WORDPRO.EXE"]
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
