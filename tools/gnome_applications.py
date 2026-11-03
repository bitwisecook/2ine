#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Searchable native application rows and their shared window actions."""

if __package__:
    from . import gnome_files, gnome_views
else:
    import gnome_files
    import gnome_views

from gi.repository import Gio

Adw, Gtk, GLib = gnome_views.Adw, gnome_views.Gtk, gnome_views.GLib


class ApplicationsPage(Gtk.Box):
    def __init__(self, window):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=18)
        self.window = window
        controls = Adw.WrapBox(child_spacing=6, line_spacing=6)
        for label, action in (
            ("Install Software…", "install"),
            ("Run Program…", "run"),
            ("Add Application…", "add-app"),
        ):
            button = Gtk.Button(label=label, action_name="win." + action)
            if action == "install":
                button.add_css_class("suggested-action")
            controls.append(button)
        self.append(controls)
        support = Adw.PreferencesGroup(title="OS/2 Support")
        self.append(support)
        for title, subtitle, action in (
            (
                "Install OS/2 Files…",
                "Browse original media and choose files to install",
                "os2-files",
            ),
            (
                "Check and Restore System Libraries…",
                "Find dependencies required by installed applications",
                "restore-libraries",
            ),
        ):
            row = Adw.ActionRow(
                title=title, subtitle=subtitle, activatable=True, action_name="win." + action
            )
            row.add_suffix(Gtk.Image(icon_name="go-next-symbolic"))
            support.add(row)
        self.search = Gtk.SearchEntry(placeholder_text="Search Applications")
        self.search.update_property([Gtk.AccessibleProperty.LABEL], ["Search applications"])
        self.append(self.search)
        self.list = Gtk.ListBox(
            selection_mode=Gtk.SelectionMode.SINGLE, activate_on_single_click=False
        )
        self.list.add_css_class("boxed-list")
        self.list.update_property(
            [Gtk.AccessibleProperty.LABEL], ["Installed application launchers"]
        )
        self.list.set_filter_func(self.matches)
        self.list.connect("row-selected", self.selected)
        self.search.set_key_capture_widget(self.list)
        self.search.connect("search-changed", self.filter)
        self.append(self.list)
        self.rows = {}
        applications = window.snapshot["manifest"].get("applications", {})
        for name, application in sorted(applications.items()):
            row = Adw.ActionRow(
                title=name, subtitle=application["program"], use_markup=False, activatable=True
            )
            row.connect("activated", lambda row: window.execute(["launch", row.application_name]))
            row.application_name = name
            row.search_text = gnome_files.folded(name + " " + application["program"])
            image = Gtk.Image(icon_name="application-x-executable-symbolic", pixel_size=40)
            window.load_application_icon(name, image)
            row.add_prefix(image)
            play = Gtk.Button(
                icon_name="media-playback-start-symbolic",
                tooltip_text="Launch " + name,
                valign=Gtk.Align.CENTER,
            )
            play.set_action_name("win.launch-application")
            play.set_action_target_value(GLib.Variant("s", name))
            play.update_property([Gtk.AccessibleProperty.LABEL], ["Launch " + name])
            row.add_suffix(play)
            menu = Gio.Menu()
            for title, action in (
                ("Launch", "launch-application"),
                ("Edit Application…", "edit-application"),
                ("Copy Program Path", "copy-application"),
                ("Remove Launcher", "remove-application"),
            ):
                item = Gio.MenuItem.new(title, None)
                item.set_action_and_target_value("win." + action, GLib.Variant("s", name))
                menu.append_item(item)
            more = Gtk.MenuButton(
                icon_name="view-more-symbolic",
                menu_model=menu,
                tooltip_text="Application Actions",
                valign=Gtk.Align.CENTER,
            )
            more.update_property([Gtk.AccessibleProperty.LABEL], ["Actions for " + name])
            row.add_suffix(more)
            gesture = Gtk.GestureClick(button=3)
            gesture.connect("pressed", window.ball_context_menu, row, menu)
            row.add_controller(gesture)
            self.list.append(row)
            self.rows[name] = row
        self.empty = Adw.StatusPage(
            title="No Matching Applications", icon_name="system-search-symbolic", visible=False
        )
        self.append(self.empty)
        if not applications:
            self.empty.set_title("A Home for Your Applications")
            self.empty.set_description(
                "Install software from your OS/2 media, then add its executable to keep it here."
            )
        self.filter()

    def matches(self, row):
        return all(
            word in row.search_text for word in gnome_files.folded(self.search.get_text()).split()
        )

    def filter(self, *_):
        self.list.invalidate_filter()
        visible = [row for row in self.rows.values() if self.matches(row)]
        selected = self.list.get_selected_row()
        if selected and selected not in visible:
            self.list.select_row(None)
        self.list.set_visible(bool(visible))
        self.empty.set_visible(not visible)
        self.search.set_visible(bool(self.rows))

    def selected(self, *_):
        self.window.update_application_actions()

    def selected_name(self):
        row = self.list.get_selected_row()
        return row.application_name if row else None
