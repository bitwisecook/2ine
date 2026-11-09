#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise native navigation, application actions, preferences and narrow registry use."""

import argparse
import json
import os
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4]))
from tests.integration.apps.gnome.gnome_ball import gnome_ball, pump
from tests.integration.harness.images import FlatGuest, linear_image
from tests.tools.test_os2_support import LEVEL


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--screenshots", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="2ine-gnome-native-") as temporary:
        root = Path(temporary)
        os.environ.update(XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"))
        api, Adw = gnome_ball.ball_api, gnome_ball.Adw
        api.configure_library(args.build.resolve())
        ball = root / "Desktop.2ineball"
        api.init(ball)
        tools = ball / "drive_c/TOOLS"
        tools.mkdir()
        guest = FlatGuest()
        guest.exit(0)
        image = linear_image(guest.code, guest.fixups, ["DOSCALLS"], "DESKTOPTEST", data=guest.data)
        for name, executable in (("Café Notes", "NOTES.EXE"), ("Calculator", "CALC.EXE")):
            (tools / executable).write_bytes(image)
            api.native_request(
                {
                    "action": "command",
                    "ball": str(ball),
                    "arguments": ["add-app", name, "C:\\TOOLS\\" + executable],
                }
            )
        for name, data in (
            ("DisplayProfile", b"\x01\0\0\0Balanced\0"),
            ("Scale", b"\x04\0\0\0\x90\0\0\0"),
        ):
            profile = api.native_request(
                {"action": "configuration-read", "ball": str(ball), "name": "registry"}
            )
            api.native_request(
                {
                    "action": "configuration-write",
                    "ball": str(ball),
                    "name": "registry",
                    "expected": profile["revision"],
                    "path": "hklm",
                    "key": "v:" + name,
                    "text": data.hex(),
                }
            )
        folder = root / "My Balls"
        folder.mkdir()
        preferences = root / "config/2ine/ball-gnome.json"
        preferences.parent.mkdir(parents=True)
        preferences.write_text(
            json.dumps(
                {
                    "ballPaths": [str(ball)],
                    "selected": str(ball),
                    "ballFolder": str(folder),
                    "logFontSize": 18,
                    "customLogSize": True,
                    "futurePreferences": {"keep": 1},
                }
            )
        )
        app = gnome_ball.BallApplication(args.build.resolve(), ball)
        app.register(None)
        app.activate()
        window = app.get_active_window()
        pump(lambda: not window.busy and window.snapshot is not None)
        window.set_default_size(1120, 820)

        def settle():
            deadline = time.monotonic() + 0.55
            pump(lambda: time.monotonic() >= deadline)

        def capture(name):
            if not args.screenshots:
                return
            from tests.integration.harness.linux.x11 import Display

            settle()
            directory = args.screenshots.resolve()
            directory.mkdir(parents=True, exist_ok=True)
            display = Display(os.environ["DISPLAY"])
            matches = [identifier for identifier, title in display.windows() if title == "2ine"]
            assert matches
            display.capture(matches[0], directory / (name + ".png"))

        assert gnome_ball.VERSION == "0.3"
        assert window.default_ball_folder() == folder
        assert window.app_preferences["logFontSize"] == 18
        window.show_page("apps")
        settle()
        page = window.applications_page
        page.sort.set_selected(1)
        assert page.list.get_row_at_index(0).application_name == "Café Notes"
        page.icon_loaded(page.rows["Calculator"], True)
        assert page.list.get_row_at_index(0).application_name == "Café Notes"
        page.sort.set_selected(0)
        assert page.list.get_row_at_index(0).application_name == "Calculator"
        assert window.find()
        page.search.set_text("cafe")
        page.search.emit("search-changed")
        assert page.rows["Café Notes"].get_child_visible()
        assert not page.rows["Calculator"].get_child_visible()
        page.list.select_row(page.rows["Café Notes"])
        assert window.lookup_action("launch-selected").get_enabled()
        capture("applications-search")
        window.activate_action("win.edit-selected", None)
        pump(lambda: window.get_visible_dialog() is not None)
        window.get_visible_dialog().close()
        pump(lambda: window.get_visible_dialog() is None)
        # Run the native row action, then the explicit window accelerator.
        pump(lambda: not window.busy)
        page.rows["Café Notes"].activate()
        pump(lambda: not window.busy and window.last_result is not None)
        assert window.last_result == 0, window.log_text
        window.show_page("apps")
        page = window.applications_page
        page.search.set_text("")
        page.search.emit("search-changed")
        page.list.select_row(page.rows["Calculator"])
        window.activate_action("win.launch-selected", None)
        pump(lambda: window.process is not None or not window.busy)
        pump(lambda: not window.busy)
        assert window.last_result == 0, window.log_text

        window.activate_action("win.remove-application", gnome_ball.GLib.Variant("s", "Calculator"))
        pump(lambda: not window.busy and "Calculator" not in window.applications_page.rows)
        window.refresh()
        pump(lambda: not window.busy)
        assert "Calculator" not in window.applications_page.rows
        window.undo(window.undo_commands[-1])
        pump(lambda: not window.busy and "Calculator" in window.applications_page.rows)

        media = root / "OS2 Media" / "OS2"
        media.mkdir(parents=True)
        (media / "CMD.EXE").write_bytes(image)
        (media / "SYSLEVEL.OS2").write_bytes(LEVEL)
        window.present_os2_media([str(media.parent)])
        recovery = window.component_pane()
        pump(lambda: recovery.directory is not None and not recovery.working)
        assert window.get_visible_dialog() is None
        capture("os2-media-browser")
        if recovery.plan is None:
            recovery.plan_sources()
        pump(lambda: recovery.component_tree is not None and not recovery.working)
        assert any(
            file["path"].replace("\\", "/").rsplit("/", 1)[-1].upper() == "CMD.EXE"
            for file in recovery.plan["files"]
            if file["source"] in recovery.component_tree.selected
        )
        capture("os2-extraction-options")
        pump(
            lambda: (
                (ball / "drive_c/OS2/CMD.EXE").is_file()
                and not window.component_active(ball)
                and not recovery.polling
            )
        )
        assert (ball / "drive_c/OS2/CMD.EXE").read_bytes() == image
        assert window.get_visible_dialog() is None
        window.audit_dependencies()
        pump(lambda: window.activity_title == "Dependency audit finished" and not window.busy)
        assert window.last_log and window.last_log.is_file()
        assert window.get_visible_dialog() is None
        capture("library-audit")
        window.show_page("settings")
        navigation = window.configuration_stack
        assert isinstance(navigation.view, Adw.NavigationView)
        assert navigation.get_visible_child_name() == "overview"
        settle()
        assert window.find()
        navigation.search.set_text("printer")
        navigation.search.emit("search-changed")
        assert navigation.rows["printing"].get_visible()
        assert not navigation.rows["registry"].get_visible()
        navigation.search.set_text("")
        navigation.search.emit("search-changed")
        capture("settings-overview")
        navigation.rows["display"].emit("activated")
        pump(lambda: navigation.get_visible_child_name() == "display")
        pump(lambda: window.display_configuration.features is not None)
        capture("graphics-sound")
        navigation.view.pop()
        pump(lambda: navigation.get_visible_child_name() == "overview")
        for name in ("environment", "printing", "files", "registry", "general"):
            navigation.rows[name].emit("activated")
            pump(lambda name=name: navigation.get_visible_child_name() == name)
            settle()
            navigation.view.pop()
            pump(lambda: navigation.get_visible_child_name() == "overview")
        pump(lambda: not window.busy)

        dialog = window.preferences_dialog()
        assert isinstance(dialog, Adw.PreferencesDialog)
        assert str(folder) in dialog.folder.get_subtitle()
        dialog.size.set_value(19)
        folder = root / "Office Balls"
        folder.mkdir()
        dialog.set_folder(folder)
        pump(
            lambda: (
                preferences.exists()
                and json.loads(preferences.read_text()).get("logFontSize") == 19
                and json.loads(preferences.read_text()).get("ballFolder") == str(folder)
            )
        )
        assert json.loads(preferences.read_text())["futurePreferences"] == {"keep": 1}
        capture("preferences")
        dialog.close()
        pump(lambda: window.get_visible_dialog() is None)
        entries = window.new_ball()
        assert entries["location"].get_text() == str(folder)
        window.get_visible_dialog().close()
        pump(lambda: window.get_visible_dialog() is None)
        collision = folder / "Existing.2ineball"
        api.init(collision)
        entries = window.new_ball()
        entries["name"].set_text("Existing")
        failed = window.get_visible_dialog()
        failed.accept.emit("clicked")
        pump(lambda: not window.busy and failed.error.get_revealed())
        assert window.get_visible_dialog() is failed
        assert entries["name"].get_text() == "Existing"
        failed.close()
        pump(lambda: window.get_visible_dialog() is None)
        dialog = gnome_ball.navigation.shortcuts(window)
        assert isinstance(dialog, Adw.ShortcutsDialog)
        capture("keyboard-shortcuts")
        dialog.close()
        pump(lambda: window.get_visible_dialog() is None)

        window.show_page("settings")
        window.configuration_stack.set_visible_child_name("registry")
        registry = next(page for page in window.profile_pages if page.area == "registry")
        pump(lambda: registry.snapshot is not None)
        window.set_default_size(560, 620)
        pump(lambda: window.get_width() <= 600)
        pump(lambda: registry.split.get_collapsed())
        assert registry.keys_button.get_visible()
        assert not registry.split.get_show_sidebar()
        registry.keys_button.set_active(True)
        pump(lambda: registry.split.get_show_sidebar())
        capture("registry-narrow-keys")
        registry.select_key("hklm")
        pump(lambda: not registry.split.get_show_sidebar())
        assert registry.selected_key == "hklm"
        capture("registry-narrow-values")
        window.set_default_size(1120, 820)
        pump(lambda: not registry.split.get_collapsed())
        assert registry.split.get_show_sidebar()
        window.show_page("settings")
        window.configuration_stack.set_visible_child_name("overview")
        Adw.StyleManager.get_default().set_color_scheme(Adw.ColorScheme.FORCE_DARK)
        capture("settings-dark")
        missing = root / "Moved.2ineball"
        api.init(missing)
        (missing / "drive_c/retained.txt").write_text("kept")
        window.remember(missing)
        pump(lambda: not window.busy and window.ball == missing)
        moved = root / "Elsewhere.2ineball"
        missing.rename(moved)
        window.refresh(missing)
        pump(lambda: not window.busy and window.ball == missing and window.snapshot is None)
        assert window.lookup_action("forget-ball").get_enabled()
        assert not window.lookup_action("install").get_enabled()
        capture("missing-ball")
        window.activate_action("win.forget-ball", None)
        pump(
            lambda: (
                not window.busy and missing not in window.external and window.snapshot is not None
            )
        )
        assert (moved / "drive_c/retained.txt").read_text() == "kept"
        app.quit()
        print(
            "Native settings/back navigation, application search/launch, preferences persistence, shortcuts and adaptive registry passed"
        )


if __name__ == "__main__":
    main()
