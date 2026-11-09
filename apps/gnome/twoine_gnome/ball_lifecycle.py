# Public domain. No rights reserved. Wholly AI-authored.
"""Package-only ball targets and machine-local references for GTK management."""

import stat
from pathlib import Path


def package(path, expected=None):
    path = Path(path).expanduser().absolute()
    if path.is_symlink() or not stat.S_ISDIR(path.lstat().st_mode):
        raise ValueError("Choose a ball package directory, not a symbolic link.")
    path = path.resolve(strict=True)
    if path == Path.home().resolve() or path.parent == path or path.suffix.lower() != ".2ineball":
        raise ValueError("Choose a .2ineball package.")
    manifest = path / "ball.json"
    if not stat.S_ISREG(manifest.lstat().st_mode):
        raise ValueError("The selected directory is not a ball package.")
    if expected is not None and identity(path) != expected:
        raise ValueError("The ball changed on disk. Select it again before continuing.")
    return path


def identity(path):
    info = Path(path).lstat()
    return info.st_dev, info.st_ino


def rename_destination(path, name):
    source = package(path)
    name = name.strip()
    if name.lower().endswith(".2ineball"):
        name = name[:-9]
    if (
        not name
        or name in (".", "..")
        or any(c in "/\\" or ord(c) < 32 or ord(c) == 127 for c in name)
    ):
        raise ValueError("Choose a ball name without slashes or control characters.")
    destination = source.with_name(name + ".2ineball")
    if destination != source and (destination.exists() or destination.is_symlink()):
        raise ValueError("A file or ball with that name already exists.")
    return destination


def update_references(window, source, destination=None):
    source = Path(source).resolve()
    destination = Path(destination) if destination is not None else None

    def matches(value):
        return Path(value).resolve() == source

    window.external = sorted(
        {
            destination if matches(p) else p
            for p in window.external
            if not matches(p) or destination is not None
        }
    )
    for name in ("drafts", "settings_drafts", "settings_file_paths", "browse_locations"):
        records = getattr(window, name)
        for key in list(records):
            if not matches(key):
                continue
            value = records.pop(key)
            if destination is not None:
                records[destination if isinstance(key, Path) else str(destination)] = value
    for key in list(window.settings_file_drafts):
        if matches(key[0]):
            value = window.settings_file_drafts.pop(key)
            if destination is not None:
                window.settings_file_drafts[(str(destination), *key[1:])] = value
    for name in ("undo_commands", "redo_commands"):
        records = getattr(window, name)
        records[:] = [item for item in records if not matches(item["ball"])]
    window.icon_cache = {
        key: value for key, value in window.icon_cache.items() if not matches(key[0])
    }
    window.icon_requests = {key for key in window.icon_requests if not matches(key[0])}
    if window.last_log is not None:
        log = Path(window.last_log)
        if log.is_relative_to(source):
            window.last_log = destination / log.relative_to(source) if destination else None
    if window.ball is not None and matches(window.ball):
        window.ball = destination
        if destination is None:
            window.snapshot = None
            window.last_result = None
            window.log_text = ""
            window.install_summary = ""
