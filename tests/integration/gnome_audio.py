#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Exercise native bank installation, channel editors, persistence and About credits."""

import argparse
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.integration.gnome_balls import gnome_balls, pump
from tests.integration.midi_banks import fixtures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--screenshots", type=Path)
    args = parser.parse_args()
    build = args.build.resolve()

    def capture(name):
        if not args.screenshots:
            return
        import time

        from linux.x11 import Display

        directory = args.screenshots.resolve()
        directory.mkdir(parents=True, exist_ok=True)
        deadline = time.monotonic() + 0.6
        pump(lambda: time.monotonic() >= deadline)
        display = Display(os.environ["DISPLAY"])
        matches = [identifier for identifier, title in display.windows() if title == "2ine"]
        assert matches
        display.capture(matches[0], directory / (name + ".png"))

    with tempfile.TemporaryDirectory(prefix="2ine-gnome-audio-") as temporary:
        root = Path(temporary)
        os.environ["XDG_CONFIG_HOME"] = str(root / "config")
        os.environ["XDG_DATA_HOME"] = str(root / "data")
        balls = gnome_balls.balls
        balls.configure_library(build)
        ball = root / "Audio.2ineball"
        balls.init(ball)
        app = gnome_balls.BallApplication(build, ball)
        app.register(None)
        app.activate()
        window = app.get_active_window()
        pump(lambda: not window.busy and window.snapshot is not None)
        dialog = window.audio_dialog()
        pump(lambda: dialog.data is not None and not window.busy)
        assert len(dialog.channels) == 16
        window.set_default_size(1180, 820)
        fixture_root = root / "fixtures"
        fixture_root.mkdir()
        sfz = fixtures(fixture_root, include_compressed=False)[-1]
        dialog.import_file(sfz)
        pump(lambda: not window.busy and len(dialog.data["banks"]) == 2)
        capture("sound-bank-library")
        dialog.bank.set_selected(1)
        dialog.profile.set_selected(dialog.profile_ids.index("mt32"))
        dialog.defaults()
        assert dialog.channels[0][-1].get_active()
        assert dialog.channels[1][0].get_selected() == 0
        dialog.profile.set_selected(dialog.profile_ids.index("xg"))
        dialog.defaults()
        target, bank_choice, program, number, percussion, muted = dialog.channels[1]
        assert not program.get_sensitive()  # SFZ supplies its own instrument definition.
        target.set_selected(15)
        bank_choice.set_selected(1)
        assert program.get_sensitive()
        program.set_value(7)
        number.set_value(4)
        percussion.set_selected(1)
        muted.set_active(True)
        expected = dialog.settings()
        row = target.get_ancestor(gnome_balls.Adw.ExpanderRow)
        row.set_expanded(True)
        target.grab_focus()
        if args.screenshots:
            import time

            deadline = time.monotonic() + 0.4
            pump(lambda: time.monotonic() >= deadline)
            scroll = target.get_ancestor(gnome_balls.Gtk.ScrolledWindow)
            success, bounds = target.compute_bounds(scroll.get_child())
            assert success
            scroll.get_vadjustment().set_value(max(0, bounds.get_y() - 24))
        capture("midi-channel-mapping")
        dialog.submit()
        pump(lambda: not window.busy and window.get_visible_dialog() is None)
        saved = balls.native_request({"action": "audio-snapshot", "ball": str(ball)})
        assert saved["settings"] == expected
        installed = ball / saved["banks"][-1]["file"]
        assert (installed.parent / "tone.wav").is_file()
        assert (installed.parent / "LICENSE.txt").is_file()
        window.about()
        pump(lambda: hasattr(window, "about_dialog"))
        about = window.about_dialog
        assert "Ryan C. Gordon" in about.get_developer_name()
        assert "Ryan C. Gordon" in about.get_copyright()
        assert about.get_license_type() == gnome_balls.Gtk.License.CUSTOM
        capture("about-credits")

        def find(widget, predicate):
            if predicate(widget):
                return widget
            child = widget.get_first_child()
            while child:
                found = find(child, predicate)
                if found:
                    return found
                child = child.get_next_sibling()
            return None

        legal = find(
            about,
            lambda widget: (
                isinstance(widget, gnome_balls.Adw.ActionRow)
                and widget.get_title().replace("_", "") == "Legal"
            ),
        )
        assert legal
        assert legal.activate_action("navigation.push", gnome_balls.GLib.Variant("s", "legal"))
        navigation = legal.get_ancestor(gnome_balls.Adw.NavigationView)
        pump(lambda: navigation.get_visible_page().get_tag() == "legal")
        page = navigation.get_visible_page()
        assert find(
            page,
            lambda widget: (
                isinstance(widget, gnome_balls.Gtk.Label) and "FluidSynth" in widget.get_text()
            ),
        )
        capture("project-sources-and-licenses")
        about.close()
        pump(lambda: window.get_visible_dialog() is None)
        app.quit()
        print(
            "Native sound-bank import, 16-channel mapping, saved overrides and Ryan/source About credits passed"
        )


if __name__ == "__main__":
    main()
