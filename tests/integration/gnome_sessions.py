#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise real GTK concurrent guest sessions and shared owned service leases."""

import argparse
import json
import os
import sys
import tempfile
from pathlib import Path
from typing import TYPE_CHECKING

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from images import FlatGuest, linear_image

from tests.integration.gnome_ball import ball_api, click, pump

if TYPE_CHECKING:
    from apps.gnome.twoine_gnome import app as gnome_ball
    from apps.gnome.twoine_gnome import sessions as session_model
else:
    from tests.integration.gnome_ball import gnome_ball

    session_model = gnome_ball.sessions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="2ine-gtk-sessions-") as temporary:
        root = Path(temporary)
        os.environ.update(XDG_DATA_HOME=str(root / "data"), XDG_CONFIG_HOME=str(root / "config"))
        ball_api.configure_library(args.build.resolve())
        first, other = (ball_api.ball_home() / (name + ".2ineball") for name in ("First", "Other"))
        guest = FlatGuest()
        guest.call(1, 229, 30000)
        guest.exit(0)
        executable = linear_image(
            guest.code, guest.fixups, ["DOSCALLS"], "GTKSESS", data=guest.data
        )
        for ball in (first, other):
            ball_api.init(ball)
            for name in ("APP1.EXE", "APP2.EXE", "SERVICE.EXE"):
                (ball / "drive_c/TEMP" / name).write_bytes(executable)
        manifest = json.loads((first / "ball.json").read_text())
        manifest["startupPrograms"] = [
            {"program": r"C:\TEMP\SERVICE.EXE", "arguments": [], "owner": "fixture"}
        ]
        (first / "ball.json").write_text(json.dumps(manifest))
        app = gnome_ball.BallApplication(args.build.resolve(), first)
        app.set_flags(app.get_flags() | gnome_ball.Gio.ApplicationFlags.NON_UNIQUE)
        app.register(None)
        app.activate()
        window = app.get_active_window()
        pump(lambda: not window.busy and window.ball == first)
        assert window.execute(["run", r"C:\TEMP\APP1.EXE"])
        pump(
            lambda: (
                len(window.sessions.active(first)) == 1
                and window.sessions.active(first)[0].state == "running"
            )
        )
        first_session = window.sessions.active(first)[0]
        assert window.execute(["run", r"C:\TEMP\APP2.EXE"])
        pump(
            lambda: (
                len(window.sessions.active(first)) == 2
                and all(session.state == "running" for session in window.sessions.active(first))
            )
        )
        second_session = window.sessions.active(first)[1]
        pool = session_model.STARTUP_POOL
        assert len(pool.records) == 1
        owned = next(iter(pool.records.values()))
        service = owned["process"]
        assert len(owned["users"]) == 2
        assert window.sidebar.get_sensitive()
        assert not window.execute(["install", r"C:\TEMP\APP1.EXE"])
        window.refresh(other)
        pump(lambda: window.ball == other and not window.operation_busy)
        assert len(window.sessions.active(first)) == 2
        assert window.lookup_action("run").get_enabled()
        window.refresh(first)
        pump(lambda: window.ball == first and not window.operation_busy)
        window.selected_session = first_session.id
        window.refresh_session_selector()
        window.stop_process()
        # The confirmation retains its captured target even if selection changes.
        window.selected_session = second_session.id
        window.refresh_session_selector()
        assert click(window.get_visible_dialog(), "Stop Application")
        pump(lambda: not first_session.active)
        assert second_session.state == "running"
        assert service.twoine_alive and len(next(iter(pool.records.values()))["users"]) == 1
        window.stop_process()
        assert click(window.get_visible_dialog(), "Stop Application")
        pump(lambda: not second_session.active and not service.twoine_alive)
        assert not pool.records
        assert first_session.log != second_session.log
        assert first_session.code is not None and second_session.code is not None
        window.close()
        pump(lambda: not window.get_visible())
    print("GTK independent sessions, targeted stop and shared owned startup cleanup passed")


if __name__ == "__main__":
    main()
