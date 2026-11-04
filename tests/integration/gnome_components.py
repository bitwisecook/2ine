#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise inline GTK component jobs while navigating between disposable balls."""

import argparse
import os
import sys
import tempfile
import threading
from pathlib import Path
from typing import TYPE_CHECKING

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.integration.gnome_ball import ball_api, pump

if TYPE_CHECKING:
    from tools import gnome_ball
else:
    from tests.integration.gnome_ball import gnome_ball
from tests.tools.test_os2_support import CMD, LEVEL, MESSAGE


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="2ine-gtk-components-") as temporary:
        root = Path(temporary)
        os.environ.update(XDG_DATA_HOME=str(root / "data"), XDG_CONFIG_HOME=str(root / "config"))
        ball_api.configure_library(args.build.resolve())
        first, other = (ball_api.ball_home() / (name + ".2ineball") for name in ("First", "Other"))
        for ball in (first, other):
            ball_api.init(ball)
        media = root / "media/OS2"
        media.mkdir(parents=True)
        for name, data in (("CMD.EXE", CMD), ("SYSLEVEL.OS2", LEVEL), ("OSO001.MSG", MESSAGE)):
            (media / name).write_bytes(data)
        app = gnome_ball.BallApplication(args.build.resolve(), first)
        app.set_flags(app.get_flags() | gnome_ball.Gio.ApplicationFlags.NON_UNIQUE)
        app.register(None)
        app.activate()
        window = app.get_active_window()
        pump(lambda: not window.busy and window.ball == first)
        window.present_os2_media([str(media.parent)], {"cmd"})
        pane = window.component_pane()
        pump(lambda: pane.plan is not None and not pane.working and not pane.polling)
        assert pane.get_parent() is not None
        assert window.get_visible_dialog() is None, "Component browser must remain inline"
        assert window.app_preferences["lastOS2MediaSources"]

        # Hold owned task I/O before entering the core so navigation has a deterministic gate.
        entered, release = threading.Event(), threading.Event()
        original = window.component_api

        def held(action, **fields):
            if action == "import-os2-support":
                entered.set()
                if not release.wait(30):
                    raise ValueError("Component navigation gate timed out")
            return original(action, **fields)

        window.component_api = held
        pane.install_selected()
        pump(entered.is_set)
        assert window.component_active(first)
        assert not window.busy
        assert window.sidebar.get_sensitive()
        assert pane.component_tree.active_ids
        assert not window.lookup_action("run").get_enabled()
        assert window.rename_ball(first) is None
        window.refresh(other)
        pump(lambda: not window.busy and window.ball == other)
        assert window.component_active(first)
        assert not window.component_active(other)
        assert window.lookup_action("run").get_enabled()
        assert window.get_visible_dialog() is None
        release.set()
        pump(lambda: not window.component_active(first) and not pane.polling)
        pump(lambda: any(file.get("status") == "managed" for file in pane.plan["files"]))
        assert (first / "drive_c/OS2/CMD.EXE").read_bytes() == CMD

        # Metadata-only refresh cannot replan or reopen the ISO/source.
        window.component_api = original
        command = first / "drive_c/OS2/CMD.EXE"
        command.write_bytes(CMD + b"user modification")
        pane.refresh_state()
        pump(lambda: not pane.polling)
        record = next(file for file in pane.plan["files"] if file["name"] == "CMD.EXE")
        assert record["status"] == "modified" and not record["removable"]
        pane.remove_selected()
        pump(lambda: not window.component_active(first) and not pane.polling)
        assert command.read_bytes().endswith(b"user modification")
        assert not (first / "drive_c/OS2/OSO001.MSG").exists()
        pump(lambda: all(not component.working for component in window.component_panes.values()))
        window.close()
        pump(lambda: not window.get_visible())
    print("GTK inline components: navigation, ownership status and preserved modified files passed")


if __name__ == "__main__":
    main()
