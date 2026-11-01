#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Install the GNOME ball launcher for the current user from this checkout."""

import os
import shlex
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP_ID = "io.github.bitwisecook.Twoine.Balls"


def main():
    data = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share")))
    executable = Path.home() / ".local/bin/2ine-balls"
    executable.parent.mkdir(parents=True, exist_ok=True)
    executable.write_text(
        "#!/bin/sh\nexec /usr/bin/python3 "
        + shlex.quote(str(ROOT / "tools/gnome_balls.py"))
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
    desktop.write_text(text.replace("Exec=2ine-balls", 'Exec="' + quoted + '"'))
    icon = data / "icons/hicolor/scalable/apps" / (APP_ID + ".svg")
    icon.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "tools/gnome" / icon.name, icon)
    print(f"Installed {desktop}; the source checkout must remain at {ROOT}")


if __name__ == "__main__":
    main()
