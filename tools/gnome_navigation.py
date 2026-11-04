#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Native settings navigation, application preferences and keyboard discovery."""

from pathlib import Path

if __package__:
    from . import gnome_media_preferences, gnome_views
else:
    import gnome_media_preferences
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
        media = Adw.PreferencesGroup(
            title="OS/2 Installation Media",
            description="The last verified media is reused by new balls. Each ball’s remembered source takes priority.",
        )
        page.add(media)
        self.media = Adw.ActionRow(title="Last Verified OS/2 Media", use_markup=False)
        media.add(self.media)
        self.forget_media = Adw.ActionRow(title="Forget OS/2 Media", activatable=True)
        self.forget_media.connect("activated", lambda *_: self.clear_media())
        media.add(self.forget_media)
        self.update_media()
        self.update_folder()

    def update_media(self):
        paths = gnome_media_preferences.sources(
            self.window.app_preferences.get("lastOS2MediaSources")
        )
        self.media.set_subtitle("\n".join(paths) if paths else "No verified media remembered")
        self.forget_media.set_sensitive(bool(paths))

    def clear_media(self):
        self.window.forget_os2_media()
        self.update_media()

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


class InstallationPage(Gtk.Box):
    def __init__(self, window, system=False):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=18)
        self.window, self.system = window, system
        self.append(
            Gtk.Label(
                label="OS/2 System Components" if system else "Application Installation",
                xalign=0,
                css_classes=["title-1"],
            )
        )
        self.append(
            Gtk.Label(
                label=(
                    "Choose original OS/2 media and install system components into this ball. Files supplied by 2ine are configured in Settings."
                    if system
                    else "Choose an application installer from a drive, folder or image. Installed application launchers remain in Applications."
                ),
                xalign=0,
                wrap=True,
            )
        )
        tasks = Adw.PreferencesGroup()
        self.append(tasks)
        if system:
            actions = [
                (
                    "Browse OS/2 System Components…",
                    "Reuse this ball's media, or the last verified OS/2 media",
                    lambda: window.choose_os2_support(),
                ),
                (
                    "Choose Different OS/2 Media…",
                    "Select an ISO, archive, folder or floppy images",
                    lambda: window.choose_os2_support(force_picker=True),
                ),
                (
                    "Check and Restore System Libraries…",
                    "Find required components for installed applications",
                    window.audit_dependencies,
                ),
            ]
        else:
            actions = [
                (
                    "Choose Application Installer…",
                    "Browse the ball's drives and attached media",
                    lambda: window.program_dialog("install"),
                ),
                (
                    "Manage Installation Media…",
                    "Attach a CD image or folder before starting the installer",
                    lambda: window.show_page("drives"),
                ),
                (
                    "Installed Applications",
                    "Launch and manage installed application shortcuts",
                    lambda: window.show_page("apps"),
                ),
            ]
        for title, detail, callback in actions:
            row = Adw.ActionRow(title=title, subtitle=detail, activatable=True, use_markup=False)
            row.add_suffix(Gtk.Image(icon_name="go-next-symbolic"))
            row.connect("activated", lambda _row, callback=callback: callback())
            tasks.add(row)
        if system:
            support = window.snapshot.get("os2Support", {})
            self.append(
                Gtk.Label(
                    label="CMD and installer support are installed."
                    if support.get("complete")
                    else "Original CMD and installer support are not yet complete.",
                    xalign=0,
                    wrap=True,
                    css_classes=["dim-label"],
                )
            )


def highlight_setting(widget):
    """Use the native focus target plus a temporary theme-relative highlight."""
    display = widget.get_display()
    if not hasattr(highlight_setting, "displays"):
        highlight_setting.displays = set()
    if display not in highlight_setting.displays:
        provider = Gtk.CssProvider()
        provider.load_from_string(
            ".twoine-setting-highlight { box-shadow: inset 0 0 0 2px alpha(currentColor, 0.5); background-color: alpha(currentColor, 0.07); }"
        )
        Gtk.StyleContext.add_provider_for_display(
            display, provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )
        highlight_setting.displays.add(display)
    widget.add_css_class("twoine-setting-highlight")
    widget.set_focusable(True)
    if widget.get_mapped():
        widget.grab_focus()
    else:

        def mapped(target):
            target.disconnect(handler)
            target.grab_focus()

        handler = widget.connect("map", mapped)
    GLib.timeout_add(5000, lambda: widget.remove_css_class("twoine-setting-highlight") or False)
