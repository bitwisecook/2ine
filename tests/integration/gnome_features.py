#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise native GNOME editors, image drives, imports, icons and portable archives."""

import argparse
import json
import os
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from images import FlatGuest, linear_image

from tests.integration.gnome_ball import click, pump

if os.environ.get("TWOINE_GNOME_FRONTEND"):
    sys.path.insert(0, os.environ["TWOINE_GNOME_FRONTEND"])
    from twoine_gnome import app as gnome_ball

    ball_api = gnome_ball.ball_api
else:
    from apps.gnome.twoine_gnome import app as gnome_ball
    from tools.ball import engine as ball_api


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="2ine-gnome-features-") as temporary:
        root = Path(temporary)
        os.environ["XDG_DATA_HOME"] = str(root / "data")
        os.environ["XDG_CONFIG_HOME"] = str(root / "config")
        ball = root / "Work.2ineball"
        ball_api.configure_library(args.build.resolve())
        ball_api.init(ball)
        guest = FlatGuest()
        guest.exit(0)
        executable = ball / "drive_c/TEMP/APP.EXE"
        executable.write_bytes(
            linear_image(guest.code, guest.fixups, ["DOSCALLS"], "GTKFEATURE", data=guest.data)
        )
        app = gnome_ball.BallApplication(args.build.resolve(), ball)
        app.register(None)
        app.activate()
        window = app.get_active_window()
        pump(lambda: not window.busy and window.ball == ball)
        assert window.stack.get_visible_child_name() == "components"
        window.show_page("drives")
        browser = window.drives_page.browser
        browser.load(r"C:\TEMP")
        pump(lambda: browser.directory is not None and browser.directory["path"] == r"C:\TEMP")
        assert any(entry["name"] == "APP.EXE" for entry in browser.directory["entries"])
        nested = ball / "drive_c/TEMP/Nested"
        nested.mkdir()
        (nested / "DEEP.EXE").write_bytes(executable.read_bytes())
        browser.load(r"C:\TEMP")
        pump(lambda: any(entry["name"] == "Nested" for entry in browser.directory["entries"]))
        folder_row = next(
            browser.files.model.get_item(i)
            for i in range(browser.files.model.get_n_items())
            if browser.files.model.get_item(i).get_item().data["name"] == "Nested"
        )
        folder_row.set_expanded(True)
        pump(
            lambda: any(
                browser.files.model.get_item(i).get_item().data["name"] == "DEEP.EXE"
                for i in range(browser.files.model.get_n_items())
            )
        )
        assert browser.directory["path"] == r"C:\TEMP", (
            "Disclosure must not navigate away from the parent"
        )

        browser.set_default()
        pump(lambda: not window.busy)
        assert ball_api.load(ball)["workingDirectories"]["C"] == r"C:\TEMP"

        installer = window.program_dialog("install")
        pump(lambda: installer.body.get_sensitive())
        installer.program.set_candidates([r"D:\INSTALL.CMD", r"D:\ENGLISH\INSTALL.EXE"])
        assert installer.program.get_text() == r"D:\INSTALL.CMD"
        installer.program.entry.set_text(r"C:\TEMP\APP.EXE")
        installer.program.set_candidates([r"E:\SETUP.EXE"])
        assert installer.program.get_text() == r"C:\TEMP\APP.EXE"
        installer.close()
        pump(lambda: window.get_visible_dialog() is None)

        dialog = window.program_dialog("add-app")
        pump(lambda: dialog.body.get_sensitive())
        dialog.name.set_text("Application")
        dialog.program.set_text(r"C:\TEMP\APP.EXE")
        dialog.arguments.set_text(r""""a value with spaces" "" C:\DATA""")
        row, entry = dialog.directory_rows["C"]
        row.set_enable_expansion(True)
        entry.set_text(r"C:\TEMP")
        dialog.add_variable("PATH", r"%PATH%;C:\TEMP", True)
        dialog.icon_source = str(
            Path(gnome_ball.__file__).parent / "gnome/io.github.bitwisecook.Twoine.Ball.svg"
        )
        dialog.accept.emit("clicked")
        pump(lambda: not window.busy)
        assert not dialog.error.get_revealed(), dialog.error.get_title()
        saved = ball_api.load(ball)["applications"]["Application"]
        assert saved["arguments"] == ["a value with spaces", "", r"C:\DATA"]
        assert saved["workingDirectories"]["C"] == r"C:\TEMP"
        assert saved["environment"]["PATH"] == r"%PATH%;C:\TEMP"
        assert saved["icon"].startswith("icons/")
        pump(lambda: window.get_visible_dialog() is None)
        # Edits retain opaque future metadata; Undo/Redo uses core records rather than Python copies.
        manifest_path = ball / "ball.json"
        data = json.loads(manifest_path.read_text())
        data["applications"]["Application"]["future"] = {"counter": 9223372036854775807}
        manifest_path.write_text(json.dumps(data))
        window.refresh()
        pump(lambda: not window.busy)
        editor = window.program_dialog("add-app", name="Application")
        pump(lambda: editor.body.get_sensitive())
        assert editor.arguments.get_text()
        editor.name.set_text("Renamed Application")
        editor.accept.emit("clicked")
        pump(lambda: not window.busy)
        assert (
            ball_api.load(ball)["applications"]["Renamed Application"]["future"]["counter"]
            == 9223372036854775807
        )
        pump(lambda: window.get_visible_dialog() is None)
        window.undo(window.undo_commands[-1])
        pump(lambda: not window.busy)
        assert "Application" in ball_api.load(ball)["applications"]
        window.history_operation(window.redo_commands[-1], False)
        pump(lambda: not window.busy)
        assert "Renamed Application" in ball_api.load(ball)["applications"]

        # Creation and writes use the internal HPFS filesystem; no host mount is created.
        creation = window.create_image(letter="F")
        creation.path.set_text(str(root / "Working Disk.img"))
        creation.capacity.set_value(8)
        creation.accept.emit("clicked")
        pump(lambda: not window.busy, timeout=60)
        assert not creation.error.get_revealed(), creation.error.get_title()
        assert ball_api.load(ball)["drives"]["F"]["device"] == "disk"
        pump(lambda: window.get_visible_dialog() is None)
        browser = window.drives_page.browser
        browser.load("F:\\")
        pump(lambda: browser.directory is not None and browser.directory["path"] == "F:\\")
        text = root / "A document.txt"
        text.write_text("Imported through the native GNOME UI\n")
        browser.copy_files([text], "F:\\")
        pump(lambda: not window.busy)
        listing = ball_api.native_request({"action": "browse", "ball": str(ball), "path": "F:\\"})
        assert any(entry["name"] == text.name for entry in listing["entries"])
        protection = window.device_settings("F")
        protection.readonly.set_active(True)
        protection.locked.set_active(True)
        protection.accept.emit("clicked")
        pump(lambda: not window.busy)
        assert ball_api.load(ball)["drives"]["F"]["locked"]
        pump(lambda: window.get_visible_dialog() is None)
        window.media_command(["eject", "F"])
        pump(lambda: not window.busy)
        assert "locked" in window.banner.get_title().lower()
        window.media_command(["device-lock", "F", "off"])
        pump(lambda: not window.busy)
        window.media_command(["eject", "F"])
        pump(lambda: not window.busy)
        assert "image" not in ball_api.load(ball)["drives"]["F"]

        # Verify a real saved launch still runs with the new per-app overrides and imported icon.
        window.execute(["launch", "Renamed Application"])
        pump(lambda: not window.busy)
        assert window.last_result == 0, window.log_text
        waiting = FlatGuest()
        waiting.call(1, 229, 30000)
        waiting.exit(0)
        (ball / "drive_c/TEMP/WAIT.EXE").write_bytes(
            linear_image(waiting.code, waiting.fixups, ["DOSCALLS"], "GTKWAIT", data=waiting.data)
        )
        window.execute(["run", r"C:\TEMP\WAIT.EXE"])
        pump(lambda: window.process is not None)
        original_request = ball_api.native_request

        def delayed_media(message):
            if message.get("arguments", [None])[0] == "map-image":
                time.sleep(0.3)
            return original_request(message)

        ball_api.native_request = delayed_media
        window.media_command(["map-image", "F", str(root / "Working Disk.img")])
        assert window.process is not None and window.busy and window.operation_busy
        window.stop_process()
        assert click(window.get_visible_dialog(), "Stop Application")
        pump(lambda: not window.busy)
        ball_api.native_request = original_request
        assert window.pending_operations == 0
        assert window.snapshot["manifest"]["drives"]["F"]["image"]
        assert isinstance(window.last_result, int) and window.last_result != 0
        pump(lambda: window.get_visible_dialog() is None)
        archive = root / "Portable.zip"
        chooser = window.choose_save
        window.choose_save = lambda _title, _name, callback: callback(archive)
        export = window.export_ball(True)
        export.accept.emit("clicked")
        pump(lambda: not window.busy)
        assert not export.error.get_revealed(), export.error.get_title()
        assert archive.is_file()
        pump(lambda: window.get_visible_dialog() is None)
        window.choose_save = chooser
        imported = root / "Imported.2ineball"
        ball_api.native_request(
            {"action": "import-ball", "ball": str(imported), "path": str(archive)}
        )
        window.remember(imported)
        pump(lambda: not window.busy)
        assert (
            window.snapshot["manifest"]["applications"]["Renamed Application"]["icon"]
            == saved["icon"]
        )
        window.close()
        app.quit()
        print(
            "GNOME browser, per-app inheritance/icons, opaque Undo/Redo, writable images, protection, archives and LX launch passed"
        )


if __name__ == "__main__":
    main()
