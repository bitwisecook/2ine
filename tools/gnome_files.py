# Public domain. No rights reserved. Wholly AI-authored.
"""Searchable native file tables shared by guest drives and nested media."""

import datetime
import unicodedata
from pathlib import Path

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
gi.require_version("Adw", "1")
try:
    from gi.repository import Adw, Gdk, Gio, GLib, GObject, Gtk
except ImportError as error:
    raise SystemExit("GTK 4 is required: " + str(error)) from error


def display_path(path):
    value, home = str(path), str(Path.home())
    return "~" + value[len(home) :] if value == home or value.startswith(home + "/") else value


def folded(value):
    return "".join(
        char
        for char in unicodedata.normalize("NFKD", value.casefold())
        if not unicodedata.combining(char)
    )


def matches(name, query):
    remaining = iter(folded(name))
    return all(any(char == candidate for candidate in remaining) for char in folded(query))


class FileItem(GObject.Object):
    def __init__(self, entry):
        super().__init__()
        self.entry = entry


class FileList(Gtk.Box):
    styled_displays = set()

    def __init__(self, *, selected=None, activated=None, accept=None, dropped=None):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=8, vexpand=True)
        self.on_selected = selected
        self.on_activated = activated
        self.accept = accept
        self.dropped = dropped
        self.search = Gtk.SearchEntry(placeholder_text="Search Files", hexpand=True)
        self.search.update_property([Gtk.AccessibleProperty.LABEL], ["Search files"])
        self.append(self.search)
        self.store = Gio.ListStore.new(FileItem)
        self.filter = Gtk.CustomFilter.new(self.accept_item)
        self.filtered = Gtk.FilterListModel.new(self.store, self.filter)
        self.sorted = Gtk.SortListModel.new(self.filtered, None)
        self.selection = Gtk.SingleSelection.new(self.sorted)
        self.selection.set_autoselect(False)
        self.selection.set_can_unselect(True)
        self.table = Gtk.ColumnView.new(self.selection)
        self.table.set_single_click_activate(False)
        self.table.add_css_class("data-table")
        self.table.add_css_class("twoine-files")
        display = self.get_display()
        if display not in self.styled_displays:
            provider = Gtk.CssProvider()
            provider.load_from_string(
                "columnview.twoine-files row:nth-child(even):not(:selected) { background-color: alpha(currentColor, 0.035); }"
            )
            Gtk.StyleContext.add_provider_for_display(
                display, provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
            )
            self.styled_displays.add(display)
        self.table.update_property([Gtk.AccessibleProperty.LABEL], ["Files"])
        self.search.set_key_capture_widget(self.table)
        self.search.connect("search-changed", lambda *_: self.refresh())
        self.table.connect("activate", self.activate)
        self.selection.connect("notify::selected-item", self.selected)
        columns = []
        for title, key in (("Name", "name"), ("Size", "size"), ("Date Modified", "modified")):
            factory = Gtk.SignalListItemFactory()
            factory.connect("setup", self.setup_cell, key)
            factory.connect("bind", self.bind_cell, key)
            column = Gtk.ColumnViewColumn.new(title, factory)
            column.set_resizable(True)
            column.set_expand(key == "name")
            column.set_fixed_width(300 if key == "name" else 100 if key == "size" else 170)
            column.set_sorter(
                Gtk.CustomSorter.new(lambda a, b, *_, key=key: self.compare(a, b, key))
            )
            self.table.append_column(column)
            columns.append(column)
        sorter = Gtk.MultiSorter.new()
        sorter.append(Gtk.CustomSorter.new(self.compare_folder))
        sorter.append(self.table.get_sorter())
        self.sorted.set_sorter(sorter)
        self.table.sort_by_column(columns[0], Gtk.SortType.ASCENDING)
        scroll = Gtk.ScrolledWindow(vexpand=True, min_content_height=180)
        scroll.set_child(self.table)
        self.layout = Adw.BreakpointBin(
            child=scroll, vexpand=True, width_request=240, height_request=180
        )
        breakpoint = Adw.Breakpoint.new(Adw.BreakpointCondition.parse("max-width: 600sp"))
        for column in columns[1:]:
            breakpoint.add_setter(column, "visible", False)
        breakpoint.add_setter(columns[0], "fixed-width", -1)
        self.layout.add_breakpoint(breakpoint)
        self.append(self.layout)

    def accept_item(self, item, *_):
        return (not self.accept or self.accept(item.entry)) and matches(
            item.entry["name"], self.search.get_text()
        )

    @staticmethod
    def compare(a, b, key):
        left, right = a.entry.get(key), b.entry.get(key)
        if key == "name":
            left, right = (
                GLib.utf8_collate_key_for_filename(left, -1),
                GLib.utf8_collate_key_for_filename(right, -1),
            )
        else:
            left, right = left if left is not None else -1, right if right is not None else -1
        return (
            Gtk.Ordering.SMALLER
            if left < right
            else Gtk.Ordering.LARGER
            if left > right
            else Gtk.Ordering.EQUAL
        )

    @staticmethod
    def compare_folder(a, b, *_):
        return (
            Gtk.Ordering.SMALLER
            if a.entry["directory"] and not b.entry["directory"]
            else Gtk.Ordering.LARGER
            if b.entry["directory"] and not a.entry["directory"]
            else Gtk.Ordering.EQUAL
        )

    def setup_cell(self, _factory, item, key):
        if key == "name":
            box = Gtk.Box(spacing=8)
            box.append(Gtk.Image())
            box.append(Gtk.Label(xalign=0, ellipsize=3))
            if self.dropped:
                drop = Gtk.DropTarget.new(Gdk.FileList, Gdk.DragAction.COPY)
                drop.connect("drop", lambda _, files, x, y: self.drop(item, files))
                box.add_controller(drop)
            item.set_child(box)
        else:
            item.set_child(Gtk.Label(xalign=1 if key == "size" else 0))

    @staticmethod
    def bind_cell(_factory, item, key):
        entry = item.get_item().entry
        widget = item.get_child()
        if key == "name":
            widget.get_first_child().set_from_icon_name(
                "folder-symbolic"
                if entry["directory"]
                else "package-x-generic-symbolic"
                if entry.get("container")
                else "text-x-generic-symbolic"
            )
            widget.get_last_child().set_label(entry["name"])
            widget.set_tooltip_text(entry["name"])
        elif key == "size":
            widget.set_label(
                "—"
                if entry["directory"] or entry.get("size") is None
                else GLib.format_size(entry["size"])
            )
        else:
            stamp = entry.get("modified")
            try:
                text = (
                    "—"
                    if stamp is None
                    else datetime.datetime.fromtimestamp(stamp).astimezone().strftime("%x %H:%M")
                )
            except (OverflowError, OSError, ValueError):
                text = "—"
            widget.set_label(text)

    def drop(self, item, files):
        entry = item.get_item().entry if item.get_item() else None
        return bool(entry and entry["directory"] and self.dropped(files, entry["path"]))

    def set_entries(self, entries):
        self.search.set_text("")
        self.store.splice(0, self.store.get_n_items(), [FileItem(entry) for entry in entries])
        self.selection.set_selected(Gtk.INVALID_LIST_POSITION)

    def refresh(self):
        self.selection.set_selected(Gtk.INVALID_LIST_POSITION)
        self.filter.changed(Gtk.FilterChange.DIFFERENT)

    def selected(self, *_):
        item = self.selection.get_selected_item()
        if self.on_selected:
            self.on_selected(item.entry if item else None)

    def activate(self, _table, position):
        item = self.sorted.get_item(position)
        if item and self.on_activated:
            self.on_activated(item.entry)
