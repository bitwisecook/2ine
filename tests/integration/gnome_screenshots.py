#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Capture the real native GNOME views and dialogs in light and dark appearances."""

import argparse
import html
import json
import os
import sys
import time
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from linux.x11 import Display

from tests.integration.gnome_ball import pump
from tools import gnome_ball, gnome_views


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ball", required=True, type=Path)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--application", help="saved application to show in the editor")
    parser.add_argument("--activity-program", help="optional short-lived guest program to run")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    os.environ["XDG_CONFIG_HOME"] = str(output / "preferences")
    os.environ["XDG_DATA_HOME"] = str(output / "data")
    app = gnome_ball.BallApplication(args.build.resolve(), args.ball.resolve())
    app.register(None)
    app.activate()
    window = app.get_active_window()
    window.set_default_size(1180, 820)
    pump(lambda: not window.busy and window.snapshot is not None)
    display = Display(os.environ["DISPLAY"])
    screenshots = []

    def settle():
        until = time.monotonic() + 0.65
        pump(lambda: time.monotonic() > until)

    def capture(name, caption):
        settle()
        matches = [(wid, title) for wid, title in display.windows() if title == "2ine"]
        assert matches, display.windows()
        path = output / (name + ".png")
        display.capture(matches[0][0], path)
        screenshots.append({"file": path.name, "caption": caption})

    def close(dialog):
        dialog.close()
        pump(lambda: window.get_visible_dialog() is None)

    application = args.application or next(iter(window.snapshot["manifest"]["applications"]), None)
    if args.activity_program:
        window.execute(["run", args.activity_program])
        pump(lambda: not window.busy)
        assert window.last_result == 0, window.log_text
    if hasattr(window.overlay, "dismiss_all"):
        window.overlay.dismiss_all()
    for appearance, scheme in (
        ("light", gnome_ball.Adw.ColorScheme.FORCE_LIGHT),
        ("dark", gnome_ball.Adw.ColorScheme.FORCE_DARK),
    ):
        gnome_ball.Adw.StyleManager.get_default().set_color_scheme(scheme)
        window.set_default_size(1180, 820)
        window.show_page("apps")
        capture(appearance + "-applications", "Applications and extracted OS/2 icons")
        window.show_page("drives")
        browser = window.drives_page.browser
        browser.load("C:\\LOTUSW4" if "Lotus" in window.ball.name else "C:")
        pump(lambda browser=browser: browser.directory is not None)
        capture(appearance + "-drives", "Drive browser and guest folders")
        if "D" in window.snapshot["manifest"]["drives"]:
            browser.drives.set_selected(browser.letters.index("D"))
            pump(
                lambda browser=browser: (
                    browser.directory is not None and browser.directory["path"].startswith("D:")
                )
            )
            capture(
                appearance + "-installation-media",
                "Original installation media browsed without a host mount",
            )
        for page, caption in (
            ("settings", "Ball settings and inherited runtime"),
            ("config", "Conflict-aware CONFIG.SYS editor"),
            ("activity", "Actual operation result and output"),
        ):
            window.show_page(page)
            capture(appearance + "-" + page, caption)
        if application:
            dialog = window.program_dialog("add-app", name=application)
            pump(lambda dialog=dialog: dialog.body.get_sensitive())
            capture(
                appearance + "-application-general",
                "Application editor, executable and icon preview",
            )
            dialog.stack.set_visible_child_name("folders")
            capture(
                appearance + "-application-folders",
                "Independent working-folder inheritance for each drive",
            )
            dialog.stack.set_visible_child_name("environment")
            if "PATH" in dialog.environment_rows:
                dialog.add_variable("PATH", r"%PATH%;C:\LOTUSW4", True)
                dialog.environment_rows["PATH"][1].grab_focus()
            capture(
                appearance + "-application-environment",
                "Application environment inheritance and expansion",
            )
            close(dialog)
        dialog = window.program_dialog("install")
        pump(lambda dialog=dialog: dialog.body.get_sensitive())
        capture(appearance + "-install", "Installation workflow and image attachment")
        close(dialog)
        picker = gnome_views.GuestPicker(
            window, r"C:\LOTUSW4" if "Lotus" in window.ball.name else "C:", lambda _path: None
        )
        pump(lambda picker=picker: picker.browser.directory is not None)
        capture(appearance + "-guest-picker", "Native OS/2 executable picker")
        close(picker)
        dialog = window.map_dialog()
        capture(appearance + "-add-drive", "Folder mappings and allocated media devices")
        close(dialog)
        dialog = window.create_image()
        dialog.path.set_text(str(args.ball.parent / "New Disk.img"))
        capture(appearance + "-create-disk", "HPFS, FAT32 and exFAT disk creation")
        close(dialog)
        dialog = window.create_image(floppy=True)
        dialog.path.set_text(str(args.ball.parent / "New Floppy.img"))
        capture(appearance + "-create-floppy", "Formatted floppy creation")
        close(dialog)
        if "F" in window.snapshot["manifest"]["drives"]:
            dialog = window.device_settings("F")
            capture(appearance + "-device-settings", "Image write protection and device locking")
            close(dialog)
        dialog = window.export_ball()
        capture(appearance + "-export", "Portable ZIP export and image inclusion")
        close(dialog)
        window.show_page("drives")
        window.set_default_size(440, 760)
        pump(lambda: window.split.get_collapsed())
        capture(appearance + "-narrow", "Adaptive narrow-window navigation")
    (output / "manifest.json").write_text(
        json.dumps(
            {
                "application": "2ine for GNOME 0.1",
                "ball": window.ball.name,
                "screenshots": screenshots,
            },
            indent=2,
        )
        + "\n"
    )
    cards = "\n".join(
        '<figure><a href="'
        + item["file"]
        + '"><img loading="lazy" src="'
        + item["file"]
        + '" alt="'
        + html.escape(item["caption"])
        + '"></a><figcaption>'
        + html.escape(item["caption"])
        + " · "
        + item["file"].split("-", 1)[0]
        + "</figcaption></figure>"
        for item in screenshots
    )
    (output / "index.html").write_text(
        '<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>2ine — GNOME screenshots</title><style>body{font:16px system-ui;background:#f6f5f4;color:#241f31;margin:32px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(360px,1fr));gap:24px}figure{margin:0}img{width:100%;border-radius:10px;box-shadow:0 3px 12px #0002}figcaption{padding:12px 0}a{color:inherit}</style><h1>2ine for GNOME</h1><p>Actual application captures in light and dark appearances. Click a view for its full-size image.</p><main>'
        + cards
        + "</main></html>"
    )
    with zipfile.ZipFile(
        output.parent / "2ine-gnome-screenshots.zip", "w", compression=zipfile.ZIP_DEFLATED
    ) as archive:
        for path in [
            *(output / item["file"] for item in screenshots),
            output / "index.html",
            output / "manifest.json",
        ]:
            archive.write(path, path.name)
    window.close()
    app.quit()
    print(f"Captured {len(screenshots)} native GTK screenshots in {output}")


if __name__ == "__main__":
    main()
