# Public domain. No rights reserved. Wholly AI-authored.
"""Validated OS/2 media reuse in application preferences, independent of balls."""

from pathlib import Path


def sources(value):
    if not isinstance(value, list):
        return []
    return list(
        dict.fromkeys(item for item in value if isinstance(item, str) and item and "\0" not in item)
    )


def preferred(snapshot, preferences):
    local = sources((snapshot or {}).get("os2Support", {}).get("mediaSources"))
    return local or sources(preferences.get("lastOS2MediaSources"))


def verified(result):
    return sources(result.get("sources")) if result.get("complete") is True else []


def remember(preferences, result):
    selected = verified(result)
    if not selected:
        return False
    preferences["lastOS2MediaSources"] = selected
    physical = sources(result.get("physicalSources"))
    preferences["lastOS2MediaFiles"] = physical
    return True


def forget(preferences):
    changed = bool(preferences.get("lastOS2MediaSources") or preferences.get("lastOS2MediaFiles"))
    preferences.pop("lastOS2MediaSources", None)
    preferences.pop("lastOS2MediaFiles", None)
    return changed


def picker_path(preferences):
    stored = sources(preferences.get("lastOS2MediaFiles"))
    for value in stored:
        path = Path(value)
        try:
            if path.is_file() or path.is_dir():
                return path
        except OSError:
            continue
    return None
