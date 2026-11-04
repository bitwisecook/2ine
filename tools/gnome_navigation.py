#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Native settings navigation, application preferences and keyboard discovery."""

from pathlib import Path

if __package__:
    from . import gnome_views
else:
    import gnome_views

Adw, Gtk, GLib = gnome_views.Adw, gnome_views.Gtk, gnome_views.GLib

SETTINGS = {
    "general": ("General", "applications-system-symbolic", "Runtime and diagnostics"),
    "display": ("Graphics & Sound", "video-display-symbolic", "Display, video, audio and MIDI"),
    "environment": ("Environment", "utilities-terminal-symbolic", "Inherited values and overrides"),
    "printing": ("Printing", "printer-symbolic", "Default printer and output"),
    "files": ("Config Files", "document-properties-symbolic", "CONFIG.SYS and OS/2 profiles"),
    "registry": ("Registry", "view-list-symbolic", "Keys and typed values"),
}


class InputDialog(gnome_views.FormDialog):
    def __init__(self, window, title, fields, callback, accept, deferred=False):
        super().__init__(window, title, accept=accept, width=520, height=320)
        self.callback, self.deferred, self.entries = callback, deferred, {}
        self.set_default_widget(self.accept)
        group = Adw.PreferencesGroup(
            margin_top=18, margin_bottom=24, margin_start=24, margin_end=24
        )
        self.body.append(group)
        for key, label, value, browse in fields:
            row = Adw.EntryRow(title=label, text=value)
            row.connect("entry-activated", lambda *_: self.accept.activate())
            if browse:
                choose = gnome_views.action_button(
                    "Choose " + label,
                    lambda row=row, label=label, browse=browse: window.choose(
                        label, lambda path: row.set_text(str(path)), folder=browse == "folder"
                    ),
                    "folder-open-symbolic",
                )
                row.add_suffix(choose)
            group.add(row)
            self.entries[key] = row
        self.connect("map", lambda *_: next(iter(self.entries.values())).grab_focus())

    def submit(self):
        try:
            values = {key: row.get_text() for key, row in self.entries.items()}
            if self.deferred:
                self.callback(values, self)
            else:
                self.callback(values)
                self.close()
        except (OSError, ValueError, KeyError) as error:
            self.error_message(error)


class SettingsNavigation(Gtk.Box):
    """Keep settings reachable through a searchable list and native back navigation."""

    def __init__(self):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, vexpand=True)
        self.pages = {}
        self.rows = {}
        self.view = Adw.NavigationView(hhomogeneous=False, vhomogeneous=False)
        self.append(self.view)
        content = Adw.PreferencesPage()
        self.search = Gtk.SearchEntry(placeholder_text="Search Settings")
        self.search.update_property([Gtk.AccessibleProperty.LABEL], ["Search ball settings"])
        search_group = Adw.PreferencesGroup()
        search_group.add(self.search)
        content.add(search_group)
        self.group = Adw.PreferencesGroup(title="Ball Settings")
        content.add(self.group)
        self.empty = Adw.PreferencesGroup(visible=False)
        self.empty.add(Adw.ActionRow(title="No Matching Settings", use_markup=False))
        content.add(self.empty)
        self.overview = Adw.NavigationPage(title="Settings", tag="overview", child=content)
        self.view.add(self.overview)
        self.search.connect("search-changed", self.filter)

    def add_titled(self, widget, name, title):
        _, icon, description = SETTINGS[name]
        if isinstance(widget, Adw.PreferencesPage) or name in ("environment", "files", "registry"):
            child = widget
        else:
            clamp = Adw.Clamp(maximum_size=880, child=widget)
            child = Gtk.ScrolledWindow(hscrollbar_policy=Gtk.PolicyType.NEVER, child=clamp)
        toolbar = Adw.ToolbarView(content=child)
        header = Adw.HeaderBar(show_start_title_buttons=False, show_end_title_buttons=False)
        toolbar.add_top_bar(header)
        page = Adw.NavigationPage(title=title, tag=name, child=toolbar)
        self.view.add(page)
        self.pages[name] = widget
        row = Adw.ActionRow(title=title, subtitle=description, activatable=True, use_markup=False)
        row.add_prefix(Gtk.Image(icon_name=icon))
        row.add_suffix(Gtk.Image(icon_name="go-next-symbolic"))
        row.connect("activated", lambda *_: self.set_visible_child_name(name))
        self.rows[name] = row
        self.group.add(row)

    def filter(self, *_):
        query = self.search.get_text().strip().casefold()
        count = 0
        for name, row in self.rows.items():
            text = " ".join(SETTINGS[name]).casefold()
            visible = all(word in text for word in query.split())
            row.set_visible(visible)
            count += visible
        self.group.set_visible(bool(count))
        self.empty.set_visible(not count)

    def get_child_by_name(self, name):
        return self.pages.get(name)

    def get_visible_child_name(self):
        return self.view.get_visible_page().get_tag()

    def set_visible_child_name(self, name):
        if name == self.get_visible_child_name():
            return
        if name == "overview":
            self.view.pop_to_page(self.overview)
        elif name in self.pages:
            if self.get_visible_child_name() != "overview":
                self.view.pop_to_page(self.overview)
            self.view.push_by_tag(name)


def focus_search(widget):
    """Focus the mapped search entry in the current view or adaptive dialog."""
    if isinstance(widget, Gtk.SearchEntry) and widget.get_mapped() and widget.get_sensitive():
        widget.grab_focus()
        widget.select_region(0, -1)
        return True
    child = widget.get_first_child()
    while child:
        if child.get_mapped() and focus_search(child):
            return True
        child = child.get_next_sibling()
    return False


class PreferencesDialog(Adw.PreferencesDialog):
    def __init__(self, window):
        super().__init__(title="Preferences", content_width=560, content_height=450)
        self.window = window
        page = Adw.PreferencesPage()
        self.add(page)
        group = Adw.PreferencesGroup(title="New Balls")
        page.add(group)
        self.folder = Adw.ActionRow(title="Default Folder", activatable=True, use_markup=False)
        self.folder.add_suffix(Gtk.Image(icon_name="folder-open-symbolic"))
        self.folder.connect(
            "activated",
            lambda *_: window.choose("Default Ball Folder", self.set_folder, folder=True),
        )
        group.add(self.folder)
        self.reset = Adw.ActionRow(title="Use Default Folder", activatable=True)
        self.reset.add_suffix(Gtk.Image(icon_name="edit-undo-symbolic"))
        self.reset.connect("activated", lambda *_: self.set_folder(None))
        group.add(self.reset)
        group = Adw.PreferencesGroup(
            title="Logs",
            description="Text size applies to application output. Other text follows your desktop preferences.",
        )
        page.add(group)
        self.system_size = Adw.SwitchRow(title="Use Desktop Text Size")
        self.system_size.set_active(not window.app_preferences.get("customLogSize", False))
        group.add(self.system_size)
        self.size = Adw.SpinRow.new_with_range(10, 24, 1)
        self.size.set_title("Log Text Size (pt)")
        self.size.set_value(window.app_preferences.get("logFontSize", 12))
        self.size.set_sensitive(not self.system_size.get_active())
        group.add(self.size)
        self.system_size.connect("notify::active", self.font_changed)
        self.size.connect("notify::value", self.font_changed)
        self.update_folder()

    def update_folder(self):
        folder = self.window.app_preferences.get("ballFolder")
        self.folder.set_subtitle(
            gnome_views.display_path(folder or self.window.default_ball_folder())
        )
        self.reset.set_sensitive(bool(folder))

    def set_folder(self, folder):
        if folder is None:
            self.window.app_preferences.pop("ballFolder", None)
        else:
            self.window.app_preferences["ballFolder"] = str(Path(folder).resolve())
        self.update_folder()
        self.window.persist_preferences()

    def font_changed(self, *_):
        self.window.app_preferences.update(
            customLogSize=not self.system_size.get_active(), logFontSize=int(self.size.get_value())
        )
        self.size.set_sensitive(not self.system_size.get_active())
        self.window.apply_log_font()
        self.window.persist_preferences()


def shortcuts(window):
    dialog = Adw.ShortcutsDialog()
    groups = {
        "Balls": (
            ("New Ball", "new"),
            ("Open Ball", "open"),
            ("Refresh", "refresh"),
            ("Save CONFIG.SYS", "save"),
            ("Undo Ball Change", "undo"),
            ("Redo Ball Change", "redo"),
        ),
        "Applications": (
            ("Install Software", "install"),
            ("Run Program", "run"),
            ("Add Application", "add-app"),
            ("Launch Selected Application", "launch-selected"),
            ("Edit Selected Application", "edit-selected"),
            ("Stop Operation", "stop"),
        ),
        "Navigation": (
            ("Find in Current View", "find"),
            ("Drives", "page1"),
            ("Applications", "page2"),
            ("Ball Settings", "page3"),
            ("Logs", "page4"),
            ("Preferences", "preferences"),
            ("Keyboard Shortcuts", "shortcuts"),
            ("Close Window", "close"),
        ),
    }
    for title, actions in groups.items():
        section = Adw.ShortcutsSection(title=title)
        for label, action in actions:
            item = Adw.ShortcutsItem.new_from_action(label, "win." + action)
            item.set_title(label)
            section.add(item)
        dialog.add(section)
    dialog.present(window)
    return dialog
