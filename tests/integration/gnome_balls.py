#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise real GTK ball management and an LX installer through the GUI launcher."""

import argparse
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from images import FlatGuest, linear_image

if os.environ.get("TWOINE_GNOME_FRONTEND"):
    sys.path.insert(0, os.environ["TWOINE_GNOME_FRONTEND"])
    import gnome_balls

    balls = gnome_balls.balls
else:
    from tools import balls, gnome_balls

GLib = gnome_balls.GLib
Gtk = gnome_balls.Gtk


def click(widget, label):
    if isinstance(widget, Gtk.Button) and widget.get_label() == label:
        widget.emit("clicked")
        return True
    child = widget.get_first_child()
    while child:
        if click(child, label):
            return True
        child = child.get_next_sibling()
    return False


def pump(until=lambda: True, timeout=30):
    deadline = time.monotonic() + timeout
    context = GLib.MainContext.default()
    while True:
        while context.pending():
            context.iteration(False)
        if until():
            return
        if time.monotonic() >= deadline:
            raise AssertionError("GTK operation timed out")
        time.sleep(0.01)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--screenshots", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="2ine-gnome-") as temporary:
        root = Path(temporary)
        os.environ["XDG_DATA_HOME"] = str(root / "data")
        os.environ["XDG_CONFIG_HOME"] = str(root / "config")
        balls.configure_library(args.build.resolve())
        initial = balls.balls_home() / "Initial ball.2ineball"
        balls.init(initial)
        app = gnome_balls.BallApplication(args.build.resolve())
        app.register(None)
        app.activate()
        window = app.get_active_window()
        pump(lambda: not window.busy)
        assert window.ball == initial
        for name in ("runtime", "install", "run", "save"):
            assert window.lookup_action(name).get_enabled(), name
        entries = window.new_ball()
        entries["name"].set_text("Office")
        assert click(window.get_visible_dialog(), "Create")
        pump(lambda: not window.busy)
        path = balls.balls_home() / "Office.2ineball"
        assert window.ball == path
        assert window.stack.get_pages().get_n_items() == 5

        def execute(arguments, expected=0):
            window.execute(arguments)
            assert window.busy
            assert not window.sidebar.get_sensitive()
            pump(lambda: not window.busy)
            if arguments[0] in ("launch", "run", "install"):
                assert window.last_result == expected, window.log_text
            assert window.sidebar.get_sensitive()

        media = root / "Installation media"
        media.mkdir()
        g = FlatGuest()
        g.exit(0)
        executable = media / "SETUP.EXE"
        executable.write_bytes(
            linear_image(g.code, g.fixups, ["DOSCALLS"], "GNOMETEST", data=g.data)
        )
        dialog = window.map_dialog()
        dialog.source.set_text(str(media))
        dialog.letter.set_selected(dialog.letters.index("D"))
        dialog.accept.emit("clicked")
        pump(lambda: not window.busy)
        assert "D" in balls.load(path)["drives"]
        execute(["config", "runtime.backend", "unicorn"])
        execute(["config", "env.TEST_VALUE", "A value with spaces"])
        execute(
            [
                "add-app",
                "--cwd",
                r"C:\TEMP",
                "Test application",
                r"D:\SETUP.EXE",
                "argument with spaces",
                "",
            ]
        )
        assert balls.load(path)["applications"]["Test application"]["cwd"] == r"C:\TEMP"
        # Runtime selection is shared with the Mac app and survives a fresh snapshot.
        original_request = balls.native_request
        request_threads = []
        ticks = []

        def observed_request(message):
            request_threads.append(threading.get_ident())
            time.sleep(0.15)
            return original_request(message)

        balls.native_request = observed_request
        timer = GLib.timeout_add(10, lambda: ticks.append(True) or GLib.SOURCE_CONTINUE)
        window.request({"action": "runtime-build", "build": str(args.build.resolve())})
        pump(lambda: not window.busy)
        GLib.source_remove(timer)
        balls.native_request = original_request
        assert (
            ticks and request_threads and all(t != threading.get_ident() for t in request_threads)
        )
        assert balls.load(path)["runtime"]["build"] == str(args.build.resolve())
        # Drafts survive navigation and refresh; installer changes cause a visible conflict.
        window.config_editor.get_buffer().insert_at_cursor("REM café\r\n")
        draft = window.drafts[path]
        window.show_page("drives")
        window.refresh()
        pump(lambda: not window.busy)
        assert window.drafts[path] == draft
        other = root / "Other.2ineball"
        balls.init(other)
        # Desktop file arguments and --ball must reach the existing D-Bus instance.
        for options, target in (([], other), (["--ball"], path)):
            with subprocess.Popen(
                [sys.executable, gnome_balls.__file__, *options, str(target)],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            ) as secondary:
                pump(lambda: secondary.poll() is not None)
                output, _ = secondary.communicate()
                assert secondary.returncode == 0, output.decode(errors="replace")
            pump(lambda target=target: not window.busy and window.ball == target)
        window.remember(other)
        pump(lambda: not window.busy)
        assert window.ball == other and window.drafts[path] == draft
        window.remember(path)
        pump(lambda: not window.busy)
        assert window.drafts[path] == draft
        config = path / "drive_c/CONFIG.SYS"
        config.write_bytes(config.read_bytes() + b"SET INSTALLER=kept\r\n")
        window.save_config()
        pump(lambda: not window.busy)
        assert "changed on disk" in window.banner.get_title()
        assert window.drafts[path] == draft
        window.drafts.clear()
        window.refresh()
        pump(lambda: not window.busy)
        window.config_editor.get_buffer().insert_at_cursor("REM café\r\n")
        window.save_config()
        pump(lambda: not window.busy)
        assert not window.drafts
        assert b"caf\xe9" in config.read_bytes()
        assert b"INSTALLER=kept" in config.read_bytes()
        # Undo applies to the captured ball rather than whichever ball is selected later.
        execute(["unmap", "D"])
        window.undo()
        pump(lambda: not window.busy)
        assert balls.load(path)["drives"]["D"] == str(media)
        execute(["remove-app", "Test application"])
        window.remember(other)
        pump(lambda: not window.busy)
        window.undo()
        pump(lambda: not window.busy)
        assert window.ball == path and balls.load(other)["applications"] == {}
        window.remember(path)
        pump(lambda: not window.busy)
        assert balls.load(path)["applications"]["Test application"]["arguments"][-1] == ""

        assert gnome_balls.parse_arguments(r"""C:\suite.rsp "C:\My Documents\one.lwp" '' """) == [
            r"C:\suite.rsp",
            r"C:\My Documents\one.lwp",
            "",
        ]
        if args.screenshots:
            from linux.x11 import Display

            args.screenshots.mkdir(parents=True, exist_ok=True)
            display = Display(os.environ["DISPLAY"])
            for page in ("apps", "drives", "settings", "config", "activity"):
                window.stack.set_visible_child_name(page)
                until = time.monotonic() + 0.3
                pump(lambda until=until: time.monotonic() > until)
                windows = [(w, name) for w, name in display.windows() if name == "2ine Balls"]
                assert windows, display.windows()
                display.capture(windows[0][0], args.screenshots / (page + ".png"))
        execute(["launch", "Test application"])
        execute(["install", r"D:\SETUP.EXE"])
        receipt = json.loads((path / "logs/last-install.json").read_text())
        assert receipt["exit_code"] == 0
        assert receipt["command"][1] == r"D:\SETUP.EXE"
        assert receipt["runtime_id"]
        window.execute(["run", r"D:\MISSING.EXE"])
        pump(lambda: not window.busy)
        assert "program does not exist" in window.banner.get_title().casefold()
        execute(["launch", "Test application"])
        execute(["unset", "env.TEST_VALUE"])
        execute(["remove-app", "Test application"])
        execute(["unmap", "D"])
        data = balls.load(path)
        assert data["environment"] == {}
        assert data["applications"] == {}
        assert set(data["drives"]) == {"C"}
        window.set_default_size(390, 680)
        pump(lambda: window.split.get_collapsed())
        assert window.split.get_collapsed()
        assert (
            window.switcher.get_display_mode()
            == gnome_balls.Adw.InlineViewSwitcherDisplayMode.ICONS
        )
        if args.screenshots:
            until = time.monotonic() + 0.5
            pump(lambda: time.monotonic() > until)
            display.capture(windows[0][0], args.screenshots / "narrow.png")
        window.config_editor.get_buffer().insert_at_cursor("REM close-save\r\n")
        window.close()
        dialog = window.get_visible_dialog()
        assert isinstance(dialog, gnome_balls.Adw.AlertDialog)
        assert click(dialog, "Cancel")
        pump(lambda: window.get_visible_dialog() is None)
        assert path in window.drafts and window.get_visible()
        window.close()
        pump(lambda: window.get_visible_dialog() is not None)
        assert click(window.get_visible_dialog(), "Save Changes")
        pump(lambda: not window.busy and not window.drafts)
        assert not window.drafts and b"REM close-save" in config.read_bytes()
        app.quit()
        print("GNOME management, async LX launch, installer receipt and failure recovery passed")


if __name__ == "__main__":
    main()
