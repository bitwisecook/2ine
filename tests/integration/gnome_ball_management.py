#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise GTK package rename, captured menu targets and deletion confirmations."""

import argparse
import importlib
import json
import os
import sys
import tempfile
import time
from pathlib import Path
from typing import TYPE_CHECKING

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="2ine-gtk-management-") as temporary:
        root = Path(temporary).resolve()
        os.environ.update(XDG_DATA_HOME=str(root / "data"), XDG_CONFIG_HOME=str(root / "config"))
        from tools import ball as api

        if TYPE_CHECKING:
            from tools import gnome_ball as gui
        elif os.environ.get("TWOINE_GNOME_FRONTEND"):
            sys.path.insert(0, os.environ["TWOINE_GNOME_FRONTEND"])
            gui = importlib.import_module("gnome_ball")
        else:
            from tools import gnome_ball as gui

        def pump(until, timeout=30):
            deadline = time.monotonic() + timeout
            context = gui.GLib.MainContext.default()
            while True:
                while context.pending():
                    context.iteration(False)
                if until():
                    return
                assert time.monotonic() < deadline, "GTK management operation timed out"
                time.sleep(0.01)

        def click(widget, label):
            if isinstance(widget, gui.Gtk.Button) and widget.get_label() == label:
                widget.emit("clicked")
                return True
            child = widget.get_first_child()
            while child:
                if click(child, label):
                    return True
                child = child.get_next_sibling()
            return False

        api.configure_library(args.build.resolve())
        first, other = (api.ball_home() / (name + ".2ineball") for name in ("First", "Other"))
        for path in (first, other):
            api.init(path)
        payload = first / "drive_c/owned.txt"
        payload.write_bytes(b"retained package content")
        outside = root / "external-data"
        outside.mkdir()
        (outside / "keep.txt").write_bytes(b"external content")
        (other / "drive_c/link").symlink_to(outside, target_is_directory=True)
        app = gui.BallApplication(args.build.resolve())
        app.set_flags(app.get_flags() | gui.Gio.ApplicationFlags.NON_UNIQUE)
        app.register(None)
        app.activate()
        window = app.get_active_window()
        pump(lambda: not window.busy)
        window.refresh(first)
        pump(lambda: not window.busy and window.ball == first)

        for name in ("rename-ball", "trash-ball", "rename-ball-at", "trash-ball-at"):
            assert window.lookup_action(name).get_enabled()
        window.process = object()
        window.set_busy(False)
        assert not window.lookup_action("trash-ball-at").get_enabled()
        assert window.rename_ball() is None
        window.trash_ball(other)
        assert window.get_visible_dialog() is None
        window.process = None
        window.set_busy(False)

        entries = window.rename_ball()
        entries["name"].set_text("Renamed")
        assert click(window.get_visible_dialog(), "Rename")
        renamed = first.with_name("Renamed.2ineball")
        pump(lambda: not window.busy and window.ball == renamed)
        assert not first.exists()
        assert (renamed / "drive_c/owned.txt").read_bytes() == b"retained package content"
        assert other.exists()
        prefs = json.loads(window.preferences.read_text())
        assert str(first) not in prefs["ballPaths"]
        assert prefs["selected"] == str(renamed)

        # A menu command for another row must keep its captured target.
        window.lookup_action("trash-ball-at").activate(gui.GLib.Variant("s", str(other)))
        dialog = window.get_visible_dialog()
        assert other.name in dialog.get_heading()
        assert click(dialog, "Cancel")
        pump(lambda: window.get_visible_dialog() is None)
        assert other.exists() and window.ball == renamed

        # Simulate a host which explicitly reports no Trash support, without
        # touching the host's real Trash. Cancellation preserves the fixture.
        original = window.request_work
        window.request_work = lambda _work, finished: finished(False)
        expected = gui.gnome_ball_lifecycle.identity(other)
        window.remove_ball_package(other, expected)
        window.request_work = original
        dialog = window.get_visible_dialog()
        assert "Permanently delete" in dialog.get_heading()
        assert click(dialog, "Cancel")
        pump(lambda: window.get_visible_dialog() is None)
        assert other.exists()
        window.request_work = lambda _work, finished: finished(False)
        window.remove_ball_package(other, expected)
        window.request_work = original
        window.get_visible_dialog().emit("response", "delete")
        pump(lambda: not window.busy and not other.exists())
        assert window.ball == renamed
        assert (outside / "keep.txt").read_bytes() == b"external content"
        assert (renamed / "drive_c/owned.txt").exists()
        assert str(other) not in json.loads(window.preferences.read_text())["ballPaths"]
        window.close()
        app.quit()
    print("GTK rename, target binding, guards and explicit deletion confirmation passed")


if __name__ == "__main__":
    main()
