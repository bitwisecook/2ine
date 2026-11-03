# Public domain. No rights reserved. Wholly AI-authored.
"""Optional GNOME directory icons stored only in this machine's GVFS database."""

from pathlib import Path

ICON_NAME = "application-x-2ine-ball"


def decorate(path):
    """Set a themed icon without writing into the ball or replacing a custom icon."""
    path = Path(path)
    if path.suffix.lower() != ".2ineball" or not (path / "ball.json").is_file():
        return False
    try:
        import gi

        gi.require_version("Gio", "2.0")
        from gi.repository import Gio, GLib
    except (ImportError, ValueError):
        return False
    try:
        file = Gio.File.new_for_path(str(path.resolve()))
        flags = Gio.FileQueryInfoFlags.NOFOLLOW_SYMLINKS
        info = file.query_info("metadata::custom-icon,metadata::custom-icon-name", flags, None)
        current = info.get_attribute_string("metadata::custom-icon-name")
        if info.get_attribute_string("metadata::custom-icon") or current:
            return current == ICON_NAME
        return file.set_attribute_string("metadata::custom-icon-name", ICON_NAME, flags, None)
    except (OSError, GLib.Error):
        # GVFS metadata is optional (for example, headless or remote sessions).
        return False
