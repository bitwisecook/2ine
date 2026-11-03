#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise GTK profile/registry sections, typed editing and live CONFIG.SYS conflicts."""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.integration.gnome_ball import pump

if os.environ.get("TWOINE_GNOME_FRONTEND"):
    sys.path.insert(0, os.environ["TWOINE_GNOME_FRONTEND"])
    import gnome_ball
else:
    from tools import gnome_ball

ball_api = gnome_ball.ball_api


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="2ine-gnome-config-") as temporary:
        root = Path(temporary)
        os.environ["XDG_DATA_HOME"] = str(root / "data")
        os.environ["XDG_CONFIG_HOME"] = str(root / "config")
        ball_api.configure_library(args.build.resolve())
        ball = root / "Configuration.2ineball"
        ball_api.init(ball)
        (ball / "drive_c/OS2/FIXTURE.INI").write_bytes(b"[App]\r\nvalue=before\r\n")
        app = gnome_ball.BallApplication(args.build.resolve(), ball)
        app.register(None)
        app.activate()
        window = app.get_active_window()
        pump(lambda: not window.busy and window.ball == ball)
        window.show_page("settings")
        assert window.stack.get_child_by_name("config") is None
        for name in ("general", "display", "environment", "printing", "files", "registry"):
            assert window.configuration_stack.get_child_by_name(name) is not None
        window.configuration_stack.set_visible_child_name("registry")
        page = next(page for page in window.profile_pages if page.area == "registry")
        pump(lambda: page.snapshot is not None)
        dialog = gnome_ball.gnome_configuration.ProfileValueDialog(page, None)
        dialog.present(window)
        dialog.application.set_text(r"hkcu\software\fixture")
        dialog.key.set_text("count")
        dialog.kind.set_text("4")
        dialog.editor.get_buffer().set_text("2a000000")
        dialog.format.set_selected(2)
        assert dialog.text() == "42"
        dialog.editor.get_buffer().set_text("43")
        dialog.accept.emit("clicked")
        pump(lambda: not window.busy)
        assert not dialog.error.get_revealed(), dialog.error.get_title()
        entry = next(entry for entry in page.entries if entry["key"] == "v:count")
        assert entry["data"] == "040000002b000000"
        assert page.selected_key == r"hkcu\software\fixture"
        page.select_key("hkcu")
        assert page.selected_key == "hkcu"
        page.select_key(r"hkcu\software\fixture")
        assert page.selected_key == r"hkcu\software\fixture"
        stale = gnome_ball.gnome_configuration.ProfileValueDialog(page, entry)
        stale.present(window)
        current = window.api("configuration-read", name="registry")
        window.api(
            "configuration-write",
            name="registry",
            expected=current["revision"],
            path=r"hkcu\software\fixture",
            key="v:external",
            text="03000000ff",
        )
        stale.editor.get_buffer().set_text("2c000000")
        stale.accept.emit("clicked")
        pump(lambda: not window.busy)
        assert stale.error.get_revealed()
        assert "changed on disk" in stale.error.get_title()
        assert stale.text() == "2c000000"
        stale.close()
        window.configuration_stack.set_visible_child_name("files")
        pump(lambda: window.os2_files_configuration.current_record is not None)
        files = window.os2_files_configuration
        index = next(
            i
            for i, record in enumerate(files.records)
            if record["path"].upper() == r"C:\OS2\FIXTURE.INI"
        )
        files.file.set_selected(index)
        pump(
            lambda: (
                getattr(files, "file_snapshot", {}).get("path", "").upper() == r"C:\OS2\FIXTURE.INI"
            )
        )
        files.text_editor.get_buffer().set_text("[App]\r\nvalue=after\r\n")
        files.save_file.emit("clicked")
        pump(
            lambda: (
                not window.busy
                and (ball / "drive_c/OS2/FIXTURE.INI").read_bytes() == b"[App]\r\nvalue=after\r\n"
            )
        )
        files.file.set_selected(
            next(i for i, record in enumerate(files.records) if record["role"] == "config")
        )
        path = ball / "drive_c/CONFIG.SYS"
        original = path.read_bytes()
        process = subprocess.Popen(["/bin/sleep", "30"])
        # Polling uses process ownership; this isolated sleeper models a live child.
        window.process = process
        window.config_polling = False
        window.set_busy(False)
        try:
            changed = original + b"REM caf\xe9\r\n"
            path.write_bytes(changed)
            window.poll_config()
            pump(lambda: window.snapshot["configSys"] == changed.decode("latin1"))
            buffer = window.config_editor.get_buffer()
            assert buffer.get_text(
                buffer.get_start_iter(), buffer.get_end_iter(), True
            ) == changed.decode("latin1")
            buffer.insert(buffer.get_end_iter(), "SET DRAFT=kept\r\n")
            draft = buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), True)
            path.write_bytes(changed + b"SET INSTALLER=changed\r\n")
            window.poll_config()
            pump(lambda: window.config_conflict.get_revealed())
            assert buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), True) == draft
            assert not window.save_button.get_sensitive()
            assert not page.add.get_sensitive()
        finally:
            process.terminate()
            process.wait(timeout=5)
            window.process = None
            window.drafts.clear()
            window.set_busy(False)
        window.configuration_stack.set_visible_child_name("printing")
        printing = window.printing_configuration
        pump(lambda: printing.profiles is not None)
        before = window.api("configuration-read", name="user")
        assert printing.preference(before) is None
        assert printing.queue.get_selected() == 0
        assert window.api("configuration-read", name="user")["revision"] == before["revision"]
        printing.queue.set_selected(1)
        pump(lambda: not window.busy and printing.preference(printing.profiles["user"]) is not None)
        assert (
            bytes.fromhex(printing.preference(printing.profiles["user"])["data"]) == b"2INEPCL;\0"
        )
        current = window.api("configuration-read", name="user")
        window.api(
            "configuration-write",
            name="user",
            expected=current["revision"],
            path="PM_SPOOLER",
            key="QUEUE",
            text=b"CUSTOM;\0".hex(),
        )
        printing.load()
        pump(lambda: not printing.loading)
        assert printing.queue.get_selected() == 2
        assert bytes.fromhex(printing.preference(printing.profiles["user"])["data"]) == b"CUSTOM;\0"
        printing.remove()
        pump(lambda: not window.busy and printing.preference(printing.profiles["user"]) is None)
        assert printing.queue.get_selected() == 0
        window.configuration_stack.set_visible_child_name("display")
        ball_api.native_request(
            {
                "action": "command",
                "ball": str(ball),
                "arguments": ["config", "system.display_dpi", "144"],
            }
        )
        window.snapshot = window.api("snapshot")
        window.render()
        assert window.configuration_stack.get_visible_child_name() == "display"
        assert window.snapshot["manifest"]["system"]["display_dpi"] == "144"
        window.destroy()
    print(
        "GTK Settings sections, printer inheritance, typed registry, conflict guard, live CONFIG.SYS and DPI passed"
    )


if __name__ == "__main__":
    main()
