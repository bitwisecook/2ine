#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Install the GNOME ball launcher for the current user from this checkout."""

import os
import shlex
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP_ID = "io.github.bitwisecook.Twoine.Ball"


def main():
    data = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share")))
    executable = Path.home() / ".local/bin/2ine-ball"
    executable.parent.mkdir(parents=True, exist_ok=True)
    executable.write_text(
        "#!/bin/sh\nexec /usr/bin/python3 "
        + shlex.quote(str(ROOT / "tools/gnome_ball.py"))
        + ' "$@"\n'
    )
    executable.chmod(0o755)
    desktop = data / "applications" / (APP_ID + ".desktop")
    desktop.parent.mkdir(parents=True, exist_ok=True)
    text = (ROOT / "tools/gnome" / desktop.name).read_text()
    # Desktop Entry Exec quoting differs from shell quoting, including literal percent signs.
    quoted = (
        str(executable)
        .replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("`", "\\`")
        .replace("$", "\\$")
        .replace("%", "%%")
    )
    desktop.write_text(text.replace("Exec=2ine-ball", 'Exec="' + quoted + '"'))
    icon = data / "icons/hicolor/scalable/apps" / (APP_ID + ".svg")
    icon.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "tools/gnome" / icon.name, icon)
    symbolic = data / "icons/hicolor/symbolic/apps" / (APP_ID + "-symbolic.svg")
    symbolic.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "tools/gnome" / symbolic.name, symbolic)
    document_icon = data / "icons/hicolor/scalable/mimetypes/application-x-2ine-ball.svg"
    document_icon.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "apps/common/icons/TwoineBall.svg", document_icon)
    mime = data / "mime/packages" / (APP_ID + ".xml")
    mime.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "tools/gnome" / (APP_ID + ".mime.xml"), mime)
    metadata = data / "metainfo" / (APP_ID + ".metainfo.xml")
    metadata.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "tools/gnome" / metadata.name, metadata)
    for tool, directory in (
        ("update-mime-database", data / "mime"),
        ("update-desktop-database", data / "applications"),
        ("gtk-update-icon-cache", data / "icons/hicolor"),
    ):
        if executable_tool := shutil.which(tool):
            flags = ["--force", "--ignore-theme-index"] if tool == "gtk-update-icon-cache" else []
            subprocess.run([executable_tool, *flags, str(directory)], check=False)
    print(f"Installed {desktop}; the source checkout must remain at {ROOT}")


if __name__ == "__main__":
    main()
