# Public domain. No rights reserved. Wholly AI-authored.
"""Explicitly requested folder children for the shared native tree browser."""


class FolderChildren:
    def __init__(self):
        self.entries, self.pending, self.errors = {}, set(), {}

    @staticmethod
    def key(path):
        return path.casefold()

    def children(self, entry):
        if not entry.get("directory") or entry.get("placeholder"):
            return []
        key = self.key(entry["path"])
        if key in self.entries:
            return self.entries[key]
        error = self.errors.get(key)
        return [
            {
                "name": str(error) if error else "Loading…",
                "path": "loading:" + entry["path"],
                "directory": False,
                "size": None,
                "modified": None,
                "placeholder": True,
                "error": bool(error),
            }
        ]

    def begin(self, path):
        key = self.key(path)
        if key in self.pending or key in self.entries:
            return False
        self.pending.add(key)
        self.errors.pop(key, None)
        return True

    def complete(self, path, entries):
        key = self.key(path)
        self.pending.discard(key)
        self.errors.pop(key, None)
        self.entries[key] = list(entries)

    def fail(self, path, error):
        key = self.key(path)
        self.pending.discard(key)
        self.errors[key] = str(error)
