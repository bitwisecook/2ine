# Public domain. No rights reserved. Wholly AI-authored.
"""Guest-drive and nested-media adapters for the shared native browser."""

import datetime
from pathlib import Path

if __package__:
    from .gnome_browser import Column, NativeBrowser
    from .gnome_browser_model import folded as folded
    from .gnome_browser_model import matches as matches
else:
    from gnome_browser import Column, NativeBrowser
    from gnome_browser_model import folded as folded
    from gnome_browser_model import matches as matches

from gi.repository import GLib, Gtk


def display_path(path):
    value, home = str(path), str(Path.home())
    return "~" + value[len(home) :] if value == home or value.startswith(home + "/") else value


def file_size(entry):
    return (
        "—" if entry["directory"] or entry.get("size") is None else GLib.format_size(entry["size"])
    )


def file_date(entry):
    stamp = entry.get("modified")
    try:
        return (
            "—"
            if stamp is None
            else datetime.datetime.fromtimestamp(stamp).astimezone().strftime("%x %H:%M")
        )
    except (OverflowError, OSError, ValueError):
        return "—"


class FileList(NativeBrowser):
    def __init__(
        self,
        *,
        selected=None,
        activated=None,
        accept=None,
        dropped=None,
        children=None,
        expanded_changed=None,
        activity=None,
    ):
        self.dropped = dropped
        super().__init__(
            [
                Column(
                    "name",
                    "Name",
                    lambda data: data["name"],
                    width=300,
                    expand=True,
                    sort_key=lambda data: GLib.utf8_collate_key_for_filename(data["name"], -1),
                ),
                Column(
                    "size",
                    "Size",
                    file_size,
                    width=100,
                    align=1,
                    sort_key=lambda data: data.get("size") if data.get("size") is not None else -1,
                ),
                Column(
                    "modified",
                    "Date Modified",
                    file_date,
                    width=170,
                    sort_key=lambda data: (
                        data.get("modified") if data.get("modified") is not None else -1
                    ),
                ),
            ],
            title="Files",
            search_hint="Search Files",
            identity=lambda data: data["path"],
            children=children or (lambda _data: []),
            expanded_changed=expanded_changed,
            activity=activity,
            searchable=lambda data: data["name"],
            accept=accept,
            selected=selected,
            activated=activated,
            icon=lambda data: (
                "folder-symbolic"
                if data["directory"]
                else "package-x-generic-symbolic"
                if data.get("container")
                else "text-x-generic-symbolic"
            ),
            tooltip=lambda data: data["name"],
            leading_sort=lambda data: 0 if data["directory"] else 1,
            dropped=self.drop_entry if dropped else None,
        )

    def drop_entry(self, files, entry):
        return bool(entry["directory"] and self.dropped(files, entry["path"]))

    def set_entries(self, entries):
        self.set_rows(entries, reset_search=True)
        self.selection.set_selected(Gtk.INVALID_LIST_POSITION)
