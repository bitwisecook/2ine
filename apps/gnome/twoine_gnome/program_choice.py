# Public domain. No rights reserved. Wholly AI-authored.
"""Installer ranking and editable automatic choices, independent of GTK."""

import re

STEMS = ("install", "setup", "inst", "os2inst", "autorun")
EXTENSIONS = ("exe", "cmd", "bat", "com")


def candidate_rank(path, depth):
    name = path.replace("/", "\\").rsplit("\\", 1)[-1].casefold()
    if "." not in name:
        return None
    stem, extension = name.rsplit(".", 1)
    if extension not in EXTENSIONS or (depth != 0 and stem not in STEMS):
        return None
    return (
        depth,
        STEMS.index(stem) if stem in STEMS else 99,
        EXTENSIONS.index(extension),
        "\\ENGLISH\\" not in path.upper(),
        tuple(
            (1, int(part)) if part.isdigit() else (0, part.casefold())
            for part in re.split(r"(\d+)", path)
            if part
        ),
    )


class ProgramChoice:
    def __init__(self, text=""):
        self.text, self.manual = text, bool(text)
        self.candidates = []

    def edit(self, text):
        self.text, self.manual = text, True

    def update(self, candidates):
        self.candidates = []
        seen = set()
        for candidate in candidates:
            if candidate.casefold() not in seen:
                seen.add(candidate.casefold())
                self.candidates.append(candidate)
        if not self.manual:
            self.text = self.candidates[0] if self.candidates else ""
        return self.text


def installer_candidates(api, letter):
    pending, found, scanned = [(letter + ":\\", 0)], [], 0
    while pending and scanned < 96:
        path, depth = pending.pop(0)
        scanned += 1
        try:
            entries = api("browse", path=path)["entries"]
        except (OSError, ValueError):
            continue
        for entry in entries:
            name = entry["name"].casefold()
            if entry["directory"] and depth < 2:
                if depth == 0 or name in ("english", "os2") or "install" in name or "setup" in name:
                    pending.append((entry["path"], depth + 1))
            elif not entry["directory"]:
                rank = candidate_rank(entry["path"], depth)
                if rank is not None:
                    found.append((rank, entry["path"]))
    return [path for _rank, path in sorted(found)[:32]]
