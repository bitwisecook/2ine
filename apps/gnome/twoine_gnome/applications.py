#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Searchable native application rows and their shared window actions."""

from gi.repository import Gio

from . import application_order, files, views

Adw, Gtk, GLib = views.Adw, views.Gtk, views.GLib


class ApplicationsPage(Gtk.Box):
    def __init__(self, window):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=18)
        self.window = window
        controls = Adw.WrapBox(child_spacing=6, line_spacing=6)
        for label, action in (
            ("Run Program…", "run"),
            ("Add Application…", "add-app"),
        ):
            button = Gtk.Button(label=label, action_name="win." + action)
            controls.append(button)
        self.sort_mode = application_order.mode(window.app_preferences.get("applicationSort"))
        self.sort = Gtk.DropDown.new_from_strings(["Icons First", "Name", "Program"])
        self.sort.set_selected(application_order.MODES.index(self.sort_mode))
        self.sort.set_tooltip_text("Sort Applications")
        self.sort.update_property([Gtk.AccessibleProperty.LABEL], ["Sort applications"])
        self.sort.connect("notify::selected", self.sort_changed)
        controls.append(self.sort)
        self.append(controls)
        self.append(views.dependency_advice_box(window))
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
        self.list.set_sort_func(self.compare)
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
            row.application_program = application["program"]
            row.has_icon = False
            row.search_text = files.folded(name + " " + application["program"])
            image = Gtk.Image(icon_name="application-x-executable-symbolic", pixel_size=40)
            window.load_application_icon(
                name, image, lambda available, row=row: self.icon_loaded(row, available)
            )
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
            row.sessions_button = Gtk.Button(valign=Gtk.Align.CENTER)
            row.sessions_button.connect(
                "clicked", lambda _button, name=name: self.open_sessions(name)
            )
            row.add_suffix(row.sessions_button)
            remove = Gtk.Button(
                icon_name="list-remove-symbolic",
                tooltip_text="Remove Launcher",
                valign=Gtk.Align.CENTER,
            )
            remove.set_action_name("win.remove-application")
            remove.set_action_target_value(GLib.Variant("s", name))
            remove.update_property([Gtk.AccessibleProperty.LABEL], ["Remove launcher for " + name])
            row.add_suffix(remove)
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
            gesture = Gtk.GestureClick(button=views.Gdk.BUTTON_SECONDARY)
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
        self.update_sessions()

    def update_sessions(self):
        for name, row in self.rows.items():
            sessions = [
                session
                for session in self.window.sessions.active(self.window.ball)
                if session.name == name
            ]
            row.sessions_button.set_visible(bool(sessions))
            row.sessions_button.set_label(
                f"{len(sessions)} session" + ("s" if len(sessions) != 1 else "")
            )
            row.sessions_button.set_tooltip_text("View sessions for " + name)

    def open_sessions(self, name):
        sessions = [
            session
            for session in self.window.sessions.active(self.window.ball)
            if session.name == name
        ]
        if sessions:
            self.window.selected_session = sessions[-1].id
            self.window.refresh_session_selector()
            self.window.show_page("activity")
            self.window.session_changed(self.window.session_selector, None)

    def compare(self, left, right):
        left_key = application_order.key(
            left.application_name, left.application_program, left.has_icon, self.sort_mode
        )
        right_key = application_order.key(
            right.application_name, right.application_program, right.has_icon, self.sort_mode
        )
        return (left_key > right_key) - (left_key < right_key)

    def sort_changed(self, row, _property):
        self.sort_mode = application_order.MODES[row.get_selected()]
        self.window.app_preferences["applicationSort"] = self.sort_mode
        self.window.persist_preferences()
        self.list.invalidate_sort()

    def icon_loaded(self, row, available):
        row.has_icon = bool(available)
        if self.sort_mode == "icons":
            self.list.invalidate_sort()

    def matches(self, row):
        return all(word in row.search_text for word in files.folded(self.search.get_text()).split())

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
