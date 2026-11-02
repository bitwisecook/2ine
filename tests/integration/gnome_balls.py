#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise real GTK ball management and an LX installer through the GUI launcher."""

import argparse
import json
import os
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from images import FlatGuest, linear_image

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
        app = gnome_balls.BallApplication(args.build.resolve())
        app.register(None)
        app.activate()
        window = app.get_active_window()
        pump()
        entries = window.new_ball()
        entries["name"].set_text("Office")
        assert click(window.get_visible_dialog(), "Create")
        pump()
        path = balls.balls_home() / "Office"
        assert window.ball == path
        assert window.stack.get_pages().get_n_items() == 4

        def execute(arguments, expected=0):
            window.execute(arguments)
            assert window.process is not None
            assert not window.sidebar.get_sensitive()
            pump(lambda: window.process is None)
            assert window.last_result == expected, window.last_log.read_text()
            assert window.sidebar.get_sensitive()

        media = root / "Installation media"
        media.mkdir()
        g = FlatGuest()
        g.exit(0)
        executable = media / "SETUP.EXE"
        executable.write_bytes(
            linear_image(g.code, g.fixups, ["DOSCALLS"], "GNOMETEST", data=g.data)
        )
        entries = window.map_dialog()
        entries["drive"].set_text("D")
        entries["folder"].set_text(str(media))
        assert click(window.get_visible_dialog(), "Map")
        pump(lambda: window.process is None)
        assert window.last_result == 0, window.last_log.read_text()
        execute(["config", "runtime.backend", "unicorn"])
        execute(["config", "env.TEST_VALUE", "A value with spaces"])
        execute(["add-app", "Test application", r"D:\SETUP.EXE", "argument with spaces"])
        assert gnome_balls.parse_arguments(r"""C:\suite.rsp "C:\My Documents\one.lwp" '' """) == [
            r"C:\suite.rsp",
            r"C:\My Documents\one.lwp",
            "",
        ]
        if args.screenshots:
            from linux.x11 import Display

            args.screenshots.mkdir(parents=True, exist_ok=True)
            display = Display(os.environ["DISPLAY"])
            for page in ("apps", "drives", "settings"):
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
        execute(["run", r"D:\MISSING.EXE"], 2)
        assert "program does not exist" in window.last_log.read_text().casefold()
        execute(["unset", "env.TEST_VALUE"])
        execute(["remove-app", "Test application"])
        execute(["unmap", "D"])
        data = balls.load(path)
        assert data["environment"] == {}
        assert data["applications"] == {}
        assert set(data["drives"]) == {"C"}
        window.close()
        pump()
        app.quit()
        print("GNOME management, async LX launch, installer receipt and failure recovery passed")


if __name__ == "__main__":
    main()
