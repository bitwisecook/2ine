#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Native GTK 4/libadwaita manager for persistent 2ine balls."""

import argparse
import json
import os
import sys
from concurrent.futures import ThreadPoolExecutor
from datetime import UTC, datetime
from pathlib import Path

if __package__:
    from . import balls
else:
    import balls

try:
    import gi

    gi.require_version("Gtk", "4.0")
    gi.require_version("Adw", "1")
    from gi.repository import Adw, Gdk, Gio, GLib, Gtk
except (ImportError, ValueError) as error:
    raise SystemExit(
        "2ine Balls requires GTK 4, libadwaita and PyGObject. On Ubuntu install "
        "python3-gi gir1.2-gtk-4.0 gir1.2-adw-1.\n" + str(error)
    ) from error

APP_ID = "io.github.bitwisecook.Twoine.Balls"


def parse_arguments(text):
    return balls.native_request({"action": "parse-arguments", "text": text})


def button(label, callback, icon=False):
    widget = Gtk.Button(icon_name=label) if icon else Gtk.Button(label=label)
    if icon:
        name = {
            "list-add-symbolic": "Add",
            "folder-open-symbolic": "Choose folder or file",
            "view-refresh-symbolic": "Refresh ball",
            "media-playback-start-symbolic": "Launch application",
            "user-trash-symbolic": "Remove application launcher",
            "list-remove-symbolic": "Unmap drive",
            "edit-clear-symbolic": "Reset override",
            "document-edit-symbolic": "Edit environment variable",
        }.get(label, label)
        widget.set_tooltip_text(name)
        widget.update_property([Gtk.AccessibleProperty.LABEL], [name])
    widget.set_valign(Gtk.Align.CENTER)
    widget.connect("clicked", lambda *_: callback())
    return widget


def clear(box):
    while child := box.get_first_child():
        box.remove(child)


class BallWindow(Adw.ApplicationWindow):
    def __init__(self, app, build, initial=None):
        super().__init__(
            application=app, title="2ine Balls", default_width=1080, default_height=760
        )
        self.default_build = (
            build or Path(os.environ.get("TWOINE_BUILD", balls.ROOT / "build"))
        ).resolve()
        self.build = self.default_build
        balls.configure_library(self.build)
        self.ball = None
        self.snapshot = None
        self.process = None
        self.busy = False
        self.last_result = None
        self.last_log = None
        self.log_text = ""
        self.activity_title = "Ready"
        self.install_summary = ""
        self.external = []
        self.drafts = {}
        self.undo_commands = []
        self.worker = ThreadPoolExecutor(max_workers=1, thread_name_prefix="ball-engine")
        self.preferences = (
            Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config")))
            / "2ine/balls-gnome.json"
        )
        self.connect("close-request", self.close_requested)
        self.connect("destroy", lambda *_: self.worker.shutdown(wait=False))
        builder = Gtk.Builder.new_from_file(str(Path(__file__).parent / "gnome/window.ui"))
        for name in (
            "overlay",
            "split",
            "sidebar",
            "ball_list",
            "header",
            "content",
            "detail",
            "banner",
            "spinner",
        ):
            setattr(self, name, builder.get_object(name))
        self.set_content(self.overlay)
        Gtk.IconTheme.get_for_display(self.get_display()).add_search_path(
            str(Path(__file__).parent / "gnome")
        )
        self.ball_list.connect("row-selected", self.selected)
        for name, callback in (
            ("new", self.new_ball),
            ("open", self.open_ball),
            ("refresh", self.refresh),
            ("runtime", self.runtime_dialog),
            ("save", self.save_config),
            ("undo", self.undo),
            ("install", lambda: self.program_dialog("install")),
            ("run", lambda: self.program_dialog("run")),
            ("about", self.about),
            ("copy", lambda: self.get_clipboard().set(str(self.ball)) if self.ball else None),
            ("reveal", lambda: self.open_path(self.ball) if self.ball else None),
        ):
            action = Gio.SimpleAction.new(name, None)
            action.connect("activate", lambda _a, _p, callback=callback: callback())
            self.add_action(action)
        for name, shortcut in (
            ("new", "<Control>n"),
            ("open", "<Control>o"),
            ("refresh", "<Control>r"),
            ("save", "<Control>s"),
            ("undo", "<Control>z"),
            ("install", "<Control>i"),
            ("run", "<Control><Shift>r"),
        ):
            app.set_accels_for_action("win." + name, [shortcut])
        for i, page in enumerate(("apps", "drives", "settings", "config", "activity"), 1):
            action = Gio.SimpleAction.new("page" + str(i), None)
            action.connect("activate", lambda _a, _p, page=page: self.show_page(page))
            self.add_action(action)
            app.set_accels_for_action("win.page" + str(i), ["<Alt>" + str(i)])
        self.medium_breakpoint = Adw.Breakpoint.new(
            Adw.BreakpointCondition.parse("max-width: 1000sp")
        )
        self.add_breakpoint(self.medium_breakpoint)
        self.narrow_breakpoint = Adw.Breakpoint.new(
            Adw.BreakpointCondition.parse("max-width: 750sp")
        )
        self.narrow_breakpoint.add_setter(self.split, "collapsed", True)
        self.add_breakpoint(self.narrow_breakpoint)
        drop = Gtk.DropTarget.new(Gdk.FileList, Gdk.DragAction.COPY)
        drop.connect("drop", self.dropped)
        self.add_controller(drop)
        self.request_work(lambda: self.startup(initial), self.started)

    def request_work(self, work, completed, *, locked=True):
        if locked and self.busy:
            self.toast("Wait for the current operation to finish.")
            return False
        if locked:
            self.set_busy(True)
        future = self.worker.submit(work)

        def deliver():
            if locked:
                self.set_busy(False)
            try:
                value = future.result()
            except (OSError, ValueError, KeyError, TypeError) as error:
                self.report_error(error)
                return GLib.SOURCE_REMOVE
            completed(value)
            return GLib.SOURCE_REMOVE

        future.add_done_callback(lambda _future: GLib.idle_add(deliver))
        return True

    def set_busy(self, busy):
        self.busy = busy
        self.sidebar.set_sensitive(not busy)
        self.spinner.set_spinning(busy)
        self.spinner.set_visible(busy)
        for name in ("new", "open", "refresh", "runtime", "install", "run", "undo", "save"):
            self.lookup_action(name).set_enabled(
                not busy and (self.ball is not None or name in ("new", "open", "refresh"))
            )
        if hasattr(self, "stack"):
            for name in ("apps", "drives", "settings", "config"):
                self.stack.get_child_by_name(name).set_sensitive(not busy)

    def report_error(self, error):
        self.activity_title = "Operation could not be completed"
        self.update_activity()
        self.banner.set_title(str(error))
        self.banner.set_revealed(True)
        self.toast(error)

    def startup(self, initial):
        try:
            prefs = json.loads(self.preferences.read_text())
        except (OSError, ValueError):
            prefs = {}
        if not isinstance(prefs, dict):
            prefs = {}
        saved_paths = prefs.get("balls", [])
        if not isinstance(saved_paths, list):
            saved_paths = []
        if not isinstance(prefs.get("selected"), str):
            prefs.pop("selected", None)
        paths = {Path(p) for p in saved_paths if isinstance(p, str)} | {
            Path(p) for p in balls.native_request({"action": "discover"})
        }
        target = (
            initial.resolve()
            if initial
            else Path(prefs["selected"])
            if prefs.get("selected")
            else None
        )
        if target:
            paths.add(target)
        return sorted(paths), target

    def started(self, result):
        self.external, target = result
        self.refresh(target)

    def close_requested(self, *_):
        if self.busy:
            self.toast("Wait for the running application or operation to finish.")
            return True
        if self.drafts:
            dialog = Adw.AlertDialog(
                heading="Save CONFIG.SYS changes?",
                body="There are unsaved edits in one or more balls.",
            )
            dialog.add_response("cancel", "Cancel")
            dialog.add_response("discard", "Discard Changes")
            dialog.add_response("save", "Save Changes")
            dialog.set_response_appearance("save", Adw.ResponseAppearance.SUGGESTED)
            dialog.set_response_appearance("discard", Adw.ResponseAppearance.DESTRUCTIVE)
            dialog.set_default_response("save")
            dialog.set_close_response("cancel")

            def response(_dialog, choice):
                if choice == "discard":
                    self.drafts.clear()
                    self.close()
                elif choice == "save":
                    drafts = dict(self.drafts)

                    def save_all():
                        saved, error = [], None
                        for path, (base, text) in drafts.items():
                            try:
                                balls.native_request(
                                    {
                                        "action": "save-config-sys",
                                        "ball": str(path),
                                        "expected": base,
                                        "text": text,
                                    }
                                )
                            except (OSError, ValueError) as failure:
                                error = str(failure)
                                break
                            saved.append(path)
                        return saved, error

                    def saved(result):
                        paths, error = result
                        for path in paths:
                            self.drafts.pop(path, None)
                        if error:
                            self.report_error(error)
                        else:
                            self.close()

                    self.request_work(save_all, saved)

            dialog.connect("response", response)
            dialog.present(self)
            return True
        return False

    def toast(self, message, undo=None):
        toast = Adw.Toast(title=str(message))
        if undo:
            toast.set_button_label("Undo")
            toast.connect("button-clicked", lambda *_: self.undo(undo))
        self.overlay.add_toast(toast)

    def persist_preferences(self):
        data = {
            "balls": [str(p) for p in self.external],
            "selected": str(self.ball) if self.ball else None,
        }

        def save():
            self.preferences.parent.mkdir(parents=True, exist_ok=True)
            balls.save(self.preferences, data)

        self.worker.submit(save)

    def remember(self, path):
        path = path.resolve()

        def opened(snapshot):
            if path not in self.external:
                self.external.append(path)
            self.ball = path
            self.snapshot = snapshot
            self.refresh(path)

        self.request_work(
            lambda: balls.native_request({"action": "snapshot", "ball": str(path)}), opened
        )

    def refresh(self, select=None):
        if self.busy:
            return
        target = select if isinstance(select, Path) else self.ball
        self.request_work(
            lambda: sorted(
                set(self.external) | {Path(p) for p in balls.native_request({"action": "discover"})}
            ),
            lambda paths: self.populate(paths, target),
        )

    def populate(self, paths, target):
        clear(self.ball_list)
        chosen = None
        for path in paths:
            row = Adw.ActionRow(
                title=path.name.removesuffix(".2ineball"),
                subtitle=str(path),
                subtitle_lines=1,
                use_markup=False,
            )
            row.ball_path = path
            row.add_prefix(Gtk.Image(icon_name=APP_ID + "-symbolic"))
            drag = Gtk.DragSource(actions=Gdk.DragAction.COPY)
            drag.connect(
                "prepare",
                lambda _source, _x, _y, path=path: Gdk.ContentProvider.new_for_value(
                    Gdk.FileList.new_from_list([Gio.File.new_for_path(str(path))])
                ),
            )
            row.add_controller(drag)
            self.ball_list.append(row)
            if path == target:
                chosen = row
        self.ball_list.select_row(chosen or self.ball_list.get_row_at_index(0))
        if not paths:
            self.ball = None
            clear(self.content)
            page = Adw.StatusPage(
                title="Your OS/2 applications, at home",
                description="Keep each application's drives, settings and installed files together in a ball of 2ine.",
                icon_name=APP_ID,
            )
            create = button("Create a Ball", self.new_ball)
            create.add_css_class("suggested-action")
            create.set_halign(Gtk.Align.CENTER)
            page.set_child(create)
            self.content.append(page)
            self.set_busy(False)

    def selected(self, _list, row):
        if row is None or self.busy:
            return
        path = row.ball_path
        previous = self.ball

        def loaded(result):
            snapshot, self.install_summary = result
            if previous != path:
                self.last_log, self.last_result, self.log_text = None, None, ""
                self.activity_title = "Ready"
            self.ball, self.snapshot = path, snapshot
            self.build = Path(
                snapshot["manifest"].get("runtime", {}).get("build") or self.default_build
            )
            self.render()
            self.set_busy(False)
            self.persist_preferences()
            self.split.set_show_content(True)

        self.request_work(
            lambda: (
                balls.native_request({"action": "snapshot", "ball": str(path)}),
                self.read_receipt(path),
            ),
            loaded,
        )

    @staticmethod
    def read_receipt(path):
        try:
            receipt = json.loads((path / "logs/last-install.json").read_text())
            return f"Last installer: exit {receipt['exit_code']} · runtime {receipt['runtime_id'][:16]}"
        except FileNotFoundError:
            return "No installer receipt yet"
        except (OSError, ValueError, KeyError, TypeError):
            return "The last installer receipt could not be read"

    def show_page(self, name):
        if hasattr(self, "stack"):
            self.stack.set_visible_child_name(name)

    def render(self):
        previous = self.stack.get_visible_child_name() if hasattr(self, "stack") else "apps"
        clear(self.content)
        data = self.snapshot["manifest"]
        roots = {key: Path(value) for key, value in self.snapshot["roots"].items()}
        self.detail.set_title(self.ball.name.removesuffix(".2ineball"))
        self.stack = Adw.ViewStack(vexpand=True)
        if not hasattr(self, "switcher"):
            self.switcher = Adw.InlineViewSwitcher(
                display_mode=Adw.InlineViewSwitcherDisplayMode.BOTH, halign=Gtk.Align.CENTER
            )
            self.switcher.set_margin_top(6)
            self.switcher.set_margin_bottom(6)
            for breakpoint in (self.medium_breakpoint, self.narrow_breakpoint):
                breakpoint.add_setter(
                    self.switcher, "display-mode", Adw.InlineViewSwitcherDisplayMode.ICONS
                )
        self.switcher.set_stack(self.stack)
        self.content.append(self.switcher)
        self.content.append(self.stack)
        apps = self.page("apps", "Applications", "application-x-executable-symbolic")
        controls = Adw.WrapBox(child_spacing=6, line_spacing=6)
        install = button("Install Software…", lambda: self.program_dialog("install"))
        install.add_css_class("suggested-action")
        controls.append(install)
        controls.append(button("Run…", lambda: self.program_dialog("run")))
        controls.append(button("Add…", lambda: self.program_dialog("add-app")))
        apps.append(controls)
        group = Adw.PreferencesGroup(title="Applications")
        apps.append(group)
        for name, app in sorted(data.get("applications", {}).items()):
            row = Adw.ActionRow(
                title=name,
                subtitle=app["program"] + (" · " + app["cwd"] if app.get("cwd") else ""),
                use_markup=False,
                activatable=True,
            )
            row.add_prefix(Gtk.Image(icon_name="application-x-executable-symbolic"))
            row.connect("activated", lambda _r, name=name: self.execute(["launch", name]))
            row.add_suffix(
                button(
                    "media-playback-start-symbolic",
                    lambda name=name: self.execute(["launch", name]),
                    True,
                )
            )
            row.add_suffix(
                button(
                    "user-trash-symbolic",
                    lambda name=name: self.execute(["remove-app", name]),
                    True,
                )
            )
            group.add(row)
        if not data.get("applications"):
            apps.append(
                Adw.StatusPage(
                    title="A home for your applications",
                    description="Install software from your OS/2 media, then add its executable to keep it here.",
                    icon_name="application-x-executable-symbolic",
                )
            )
        drives = self.page("drives", "Drives", "drive-harddisk-symbolic")
        group = Adw.PreferencesGroup(
            title="Drive Mappings",
            description="Map a folder or extracted installation CD to an OS/2 drive.",
        )
        group.set_header_suffix(button("list-add-symbolic", self.map_dialog, True))
        drives.append(group)
        for letter, path in sorted(roots.items()):
            row = Adw.ActionRow(
                title=letter + ":", subtitle=str(path), subtitle_lines=1, use_markup=False
            )
            row.add_prefix(
                Gtk.Image(
                    icon_name="drive-harddisk-symbolic" if letter == "C" else "folder-symbolic"
                )
            )
            row.add_suffix(
                button("folder-open-symbolic", lambda path=path: self.open_path(path), True)
            )
            if letter != "C":
                row.add_suffix(
                    button(
                        "list-remove-symbolic",
                        lambda letter=letter: self.execute(["unmap", letter]),
                        True,
                    )
                )
            group.add(row)
        settings = self.page("settings", "Settings", "preferences-system-symbolic")
        group = Adw.PreferencesGroup(title="Runtime")
        settings.append(group)
        runtime = Adw.ActionRow(
            title="Runtime Build",
            subtitle=str(self.build),
            subtitle_lines=1,
            use_markup=False,
            activatable=True,
        )
        runtime.connect("activated", lambda *_: self.runtime_dialog())
        runtime.add_suffix(button("folder-open-symbolic", self.runtime_dialog, True))
        group.add(runtime)
        backend = Adw.ComboRow(
            title="Execution Backend",
            model=Gtk.StringList.new(["Build Default", "Native (x86 Linux)", "Unicorn"]),
        )
        backend.set_selected(
            {"native": 1, "unicorn": 2}.get(data.get("runtime", {}).get("backend"), 0)
        )
        backend.connect("notify::selected", self.backend_changed)
        group.add(backend)
        group = Adw.PreferencesGroup(
            title="2ine Settings", description="Reset an override to inherit the runtime default."
        )
        settings.append(group)
        for key, title in (
            ("trace_native", "Trace Native Calls"),
            ("trace_events", "Trace Events"),
        ):
            row = Adw.SwitchRow(title=title)
            row.set_active(
                str(data.get("system", {}).get(key, "0")).lower()
                in ("1", "yes", "y", "true", "t", "on")
            )
            row.connect(
                "notify::active",
                lambda row, _, key=key: self.execute(
                    ["config", "system." + key, str(int(row.get_active()))]
                ),
            )
            row.add_suffix(
                button(
                    "edit-clear-symbolic",
                    lambda key=key: self.execute(["unset", "system." + key]),
                    True,
                )
            )
            group.add(row)
        volume = Adw.EntryRow(
            title="Beep Volume (0–1)",
            text=str(data.get("system", {}).get("beep_volume", "1")),
            show_apply_button=True,
        )
        volume.connect(
            "apply", lambda row: self.execute(["config", "system.beep_volume", row.get_text()])
        )
        volume.add_suffix(
            button(
                "edit-clear-symbolic", lambda: self.execute(["unset", "system.beep_volume"]), True
            )
        )
        group.add(volume)
        group = Adw.PreferencesGroup(
            title="Environment Overrides",
            description="Overrides take precedence over CONFIG.SYS. Use OS2PATH for guest programs; PATH belongs to the host.",
        )
        group.set_header_suffix(button("list-add-symbolic", self.env_dialog, True))
        settings.append(group)
        for key, value in sorted(data.get("environment", {}).items()):
            row = Adw.ActionRow(title=key, subtitle=value, use_markup=False, activatable=True)
            row.connect("activated", lambda _r, key=key, value=value: self.env_dialog(key, value))
            row.add_suffix(
                button(
                    "document-edit-symbolic",
                    lambda key=key, value=value: self.env_dialog(key, value),
                    True,
                )
            )
            row.add_suffix(
                button(
                    "edit-clear-symbolic",
                    lambda key=key: self.execute(["unset", "env." + key]),
                    True,
                )
            )
            group.add(row)
        group = Adw.PreferencesGroup(
            title="Application State",
            description="Profiles, registry and shell metadata stay in C:\\OS2. Binary state is managed by installed applications.",
        )
        row = Adw.ActionRow(title="Open State Folder", activatable=True)
        row.connect("activated", lambda *_: self.open_path(roots["C"] / "OS2"))
        row.add_suffix(Gtk.Image(icon_name="folder-open-symbolic"))
        group.add(row)
        settings.append(group)
        config = self.page("config", "CONFIG.SYS", "document-edit-symbolic")
        config.append(
            Gtk.Label(
                label="SET and LIBPATH apply at launch. Drivers and boot commands are not executed.",
                wrap=True,
                xalign=0,
            )
        )
        self.config_editor = Gtk.TextView(
            monospace=True,
            wrap_mode=Gtk.WrapMode.NONE,
            left_margin=12,
            right_margin=12,
            top_margin=12,
            bottom_margin=12,
        )
        self.config_editor.update_property([Gtk.AccessibleProperty.LABEL], ["CONFIG.SYS contents"])
        self.config_editor.get_buffer().set_text(
            self.drafts.get(self.ball, (None, self.snapshot["configSys"]))[1]
        )
        self.config_editor.get_buffer().set_enable_undo(True)
        self.config_editor.get_buffer().connect("changed", self.config_changed)
        scroll = Gtk.ScrolledWindow(vexpand=True, min_content_height=280, has_frame=True)
        scroll.set_child(self.config_editor)
        config.append(scroll)
        controls = Gtk.Box(spacing=12)
        controls.append(button("Reload from Disk", self.reload_config))
        self.save_button = button("Save CONFIG.SYS", self.save_config)
        self.save_button.add_css_class("suggested-action")
        self.save_button.set_sensitive(self.ball in self.drafts)
        controls.append(self.save_button)
        config.append(controls)
        activity = self.page("activity", "Activity", "document-open-recent-symbolic")
        self.status = Gtk.Label(xalign=0, wrap=True)
        self.status.add_css_class("heading")
        activity.append(self.status)
        activity.append(Gtk.Label(label=self.install_summary, wrap=True, xalign=0, selectable=True))
        activity.append(
            Gtk.Label(
                label="Output preview shows the last 64 KiB. Complete logs are kept in the ball.",
                wrap=True,
                xalign=0,
                css_classes=["dim-label"],
            )
        )
        controls = Gtk.Box(spacing=12)
        controls.append(button("Open Logs", lambda: self.open_path(self.ball / "logs")))
        controls.append(button("Open Ball Folder", lambda: self.open_path(self.ball)))
        activity.append(controls)
        self.log_view = Gtk.TextView(
            editable=False,
            monospace=True,
            wrap_mode=Gtk.WrapMode.WORD_CHAR,
            left_margin=12,
            right_margin=12,
            top_margin=12,
        )
        self.log_view.update_property([Gtk.AccessibleProperty.LABEL], ["Application output"])
        scroll = Gtk.ScrolledWindow(min_content_height=220, vexpand=True, has_frame=True)
        scroll.set_child(self.log_view)
        activity.append(scroll)
        self.update_activity()
        self.stack.set_visible_child_name(previous or "apps")
        self.set_busy(self.busy)

    def page(self, name, title, icon):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=18)
        for edge in ("top", "bottom", "start", "end"):
            getattr(box, "set_margin_" + edge)(18)
        clamp = Adw.Clamp(maximum_size=780)
        clamp.set_child(box)
        scroll = Gtk.ScrolledWindow(hscrollbar_policy=Gtk.PolicyType.NEVER)
        scroll.set_child(clamp)
        self.stack.add_titled_with_icon(scroll, name, title, icon)
        return box

    def backend_changed(self, row, _):
        selected = row.get_selected()
        self.execute(
            ["unset", "runtime.backend"]
            if selected == 0
            else ["config", "runtime.backend", ("native", "unicorn")[selected - 1]]
        )

    def choose(self, title, callback, folder=False):
        dialog = Gtk.FileDialog(title=title)

        def done(dialog, result):
            try:
                file = dialog.select_folder_finish(result) if folder else dialog.open_finish(result)
                path = file.get_path()
                if path:
                    callback(Path(path))
            except GLib.Error as error:
                if not error.matches(Gtk.dialog_error_quark(), Gtk.DialogError.DISMISSED):
                    self.toast(error.message)

        if folder:
            dialog.select_folder(self, None, done)
        else:
            dialog.open(self, None, done)

    def form(self, title, fields, callback, accept="Save"):
        dialog = Adw.Dialog(title=title, content_width=520)
        view = Adw.ToolbarView()
        header = Adw.HeaderBar()
        view.add_top_bar(header)
        group = Adw.PreferencesGroup(
            margin_top=18, margin_bottom=24, margin_start=24, margin_end=24
        )
        view.set_content(group)
        entries = {}
        for key, label, value, browse in fields:
            row = Adw.EntryRow(title=label, text=value)
            if browse:
                row.add_suffix(
                    button(
                        "folder-open-symbolic",
                        lambda row=row, browse=browse, label=label: self.choose(
                            label, lambda path: row.set_text(str(path)), folder=browse == "folder"
                        ),
                        True,
                    )
                )
            group.add(row)
            entries[key] = row

        def submit():
            try:
                callback({key: row.get_text() for key, row in entries.items()})
            except (OSError, ValueError, KeyError) as error:
                self.toast(error)
                return
            dialog.close()

        apply = button(accept, submit)
        apply.add_css_class("suggested-action")
        header.pack_start(button("Cancel", dialog.close))
        header.pack_end(apply)
        dialog.set_child(view)
        dialog.present(self)
        return entries

    def new_ball(self):
        def create(values):
            name = values["name"].strip()
            if not name or name in (".", "..") or any(c in name for c in "/\\\r\n"):
                raise ValueError("Choose a ball name without slashes.")
            path = Path(values["location"]).expanduser() / (
                name if name.endswith(".2ineball") else name + ".2ineball"
            )

            def initialize():
                if path.exists():
                    raise ValueError("A ball with that name already exists.")
                return balls.native_request(
                    {"action": "command", "ball": str(path), "arguments": ["init"]}
                )

            def created(snapshot):
                self.external.append(path)
                self.ball, self.snapshot = path, snapshot
                self.refresh(path)

            self.request_work(initialize, created)

        return self.form(
            "New Ball",
            [
                ("name", "Ball Name", "", None),
                ("location", "Location", str(balls.balls_home()), "folder"),
            ],
            create,
            "Create",
        )

    def open_ball(self):
        self.choose("Open Ball", self.remember, folder=True)

    def dropped(self, _target, files, _x, _y):
        if not self.busy and len(files.get_files()) == 1:
            path = files.get_files()[0].get_path()
            if path:
                self.remember(Path(path))
                return True
        return False

    def runtime_dialog(self):
        if not self.ball:
            return

        def update(values):
            self.request(
                {
                    "action": "runtime-build",
                    "build": str(Path(values["build"]).expanduser().resolve()),
                }
            )

        return self.form(
            "Runtime Build", [("build", "2ine Build Folder", str(self.build), "folder")], update
        )

    def map_dialog(self):
        return self.form(
            "Map drive",
            [("drive", "Drive letter", "D", None), ("folder", "Host folder", "", "folder")],
            lambda values: self.execute(["map", values["drive"], values["folder"]]),
            "Map",
        )

    def env_dialog(self, key="", value=""):
        return self.form(
            "Environment variable",
            [("key", "Variable name", key, None), ("value", "Value", value, None)],
            lambda values: self.execute(["config", "env." + values["key"], values["value"]]),
        )

    def program_dialog(self, action):
        fields = []
        if action == "add-app":
            fields.append(("name", "Application name", "", None))
        fields.extend(
            [
                ("program", "Executable (OS/2 path or mapped host file)", "", "file"),
                ("arguments", "Arguments (quote values containing spaces)", "", None),
            ]
        )
        fields.append(("cwd", "Working directory (optional)", "", "folder"))

        def launch(values):
            if not values["program"] or (action == "add-app" and not values["name"].strip()):
                raise ValueError(
                    "Enter an executable and an application name when saving a launcher."
                )
            arguments = [action]
            if values["cwd"]:
                arguments.extend(["--cwd", values["cwd"]])
            if action == "add-app":
                name = values["name"].strip()
                if name in self.snapshot["manifest"]["applications"]:
                    raise ValueError("An application with that name already exists.")
                arguments.append(name)
            arguments.extend([values["program"], *parse_arguments(values["arguments"])])
            self.execute(arguments)

        self.form(
            {"install": "Install software", "run": "Run executable", "add-app": "Add application"}[
                action
            ],
            fields,
            launch,
            "Save" if action == "add-app" else "Run",
        )

    def config_changed(self, buffer):
        text = buffer.get_text(buffer.get_start_iter(), buffer.get_end_iter(), True)
        base = self.drafts.get(self.ball, (self.snapshot["configSys"], None))[0]
        if text == base:
            self.drafts.pop(self.ball, None)
        else:
            self.drafts[self.ball] = (base, text)
        self.save_button.set_sensitive(self.ball in self.drafts)

    def save_config(self):
        if self.ball not in self.drafts:
            return
        path = self.ball
        base, text = self.drafts[path]

        def saved(snapshot):
            self.drafts.pop(path, None)
            self.snapshot = snapshot
            self.render()
            self.toast("CONFIG.SYS saved")

        self.request_work(
            lambda: balls.native_request(
                {"action": "save-config-sys", "ball": str(path), "expected": base, "text": text}
            ),
            saved,
        )

    def reload_config(self):
        def reload():
            path = self.ball

            def loaded(snapshot):
                self.drafts.pop(path, None)
                self.snapshot = snapshot
                self.render()

            self.request_work(
                lambda: balls.native_request({"action": "snapshot", "ball": str(path)}), loaded
            )

        if self.ball not in self.drafts:
            reload()
            return
        dialog = Adw.AlertDialog(
            heading="Discard CONFIG.SYS changes?",
            body="Your unsaved edits will be replaced with the file on disk.",
        )
        dialog.add_response("cancel", "Cancel")
        dialog.add_response("discard", "Discard Changes")
        dialog.set_response_appearance("discard", Adw.ResponseAppearance.DESTRUCTIVE)
        dialog.set_close_response("cancel")
        dialog.connect("response", lambda _d, choice: reload() if choice == "discard" else None)
        dialog.present(self)

    def request(self, message, undo=None):
        path = self.ball
        message = {"ball": str(path), "build": str(self.build), **message}

        def changed(snapshot):
            self.snapshot = snapshot
            self.build = Path(
                snapshot["manifest"].get("runtime", {}).get("build") or self.default_build
            )
            self.banner.set_revealed(False)
            self.render()
            if undo:
                item = (path, undo)
                self.undo_commands.append(item)
                self.toast("Ball updated", item)

        return self.request_work(lambda: balls.native_request(message), changed)

    def inverse(self, arguments):
        data = self.snapshot["manifest"]
        action = arguments[0]
        if action in ("map", "unmap"):
            letter = arguments[1].upper().rstrip(":")
            old = data["drives"].get(letter)
            return ["map", letter, str((self.ball / old).resolve())] if old else ["unmap", letter]
        if action == "remove-app":
            name = arguments[1]
            old = data["applications"].get(name)
            if old:
                return [
                    "add-app",
                    *(["--cwd", old["cwd"]] if old.get("cwd") else []),
                    name,
                    old["program"],
                    *old.get("arguments", []),
                ]
        if action == "add-app":
            offset = 3 if arguments[1] == "--cwd" else 1
            name = arguments[offset]
            old = data["applications"].get(name)
            if old:
                return [
                    "add-app",
                    *(["--cwd", old["cwd"]] if old.get("cwd") else []),
                    name,
                    old["program"],
                    *old.get("arguments", []),
                ]
            return ["remove-app", name]
        if action in ("config", "unset"):
            group, key = arguments[1].split(".", 1)
            old = data.get("environment" if group == "env" else group, {}).get(key)
            return (
                ["config", arguments[1], str(old).lower() if isinstance(old, bool) else str(old)]
                if old is not None
                else ["unset", arguments[1]]
            )
        return None

    def undo(self, item=None):
        focus = self.get_focus()
        if item is None and isinstance(focus, Gtk.TextView):
            buffer = focus.get_buffer()
            if buffer.get_can_undo():
                buffer.undo()
            return
        if item is None and isinstance(focus, Gtk.Editable):
            focus.activate_action("text.undo", None)
            return
        if self.busy:
            return
        item = item or (self.undo_commands[-1] if self.undo_commands else None)
        if item is None or item not in self.undo_commands:
            return
        path, arguments = item

        def undone(snapshot):
            self.undo_commands.remove(item)
            if self.ball == path:
                self.snapshot = snapshot
                self.render()
            self.toast("Change undone")

        self.request_work(
            lambda: balls.native_request(
                {"action": "command", "ball": str(path), "arguments": arguments}
            ),
            undone,
        )

    def execute(self, arguments):
        if self.busy or not self.ball:
            return
        self.banner.set_revealed(False)
        if arguments[0] not in ("run", "install", "launch"):
            self.request({"action": "command", "arguments": arguments}, self.inverse(arguments))
            return
        path, build = self.ball, self.build

        def prepare():
            plan = balls.native_request(
                {
                    "action": "prepare-launch",
                    "ball": str(path),
                    "build": str(build),
                    "arguments": arguments,
                }
            )
            stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%S.%fZ")
            log = path / "logs" / f"gnome-{stamp}.log"
            log.parent.mkdir(parents=True, exist_ok=True)
            return plan, log

        self.activity_title = "Preparing application…"
        self.show_page("activity")
        self.request_work(prepare, lambda result: self.launch(path, *result))

    def launch(self, path, plan, log):
        launcher = Gio.SubprocessLauncher.new(Gio.SubprocessFlags.STDERR_MERGE)
        launcher.set_stdout_file_path(str(log))
        launcher.set_cwd(plan["cwd"])
        for key in plan["unsetEnvironment"]:
            launcher.unsetenv(key)
        for key, value in plan["environment"].items():
            launcher.setenv(key, value, True)
        try:
            self.process = launcher.spawnv(plan["command"])
        except GLib.Error as error:
            self.report_error(error.message)
            self.activity_title = "Could not start application"
            self.update_activity()
            return
        self.last_log, self.last_result, self.log_text = log, None, ""
        self.activity_title = "Running… Close the application's windows to finish."
        self.set_busy(True)
        self.update_activity()
        self.process.wait_async(None, self.completed, (path, plan, log))
        self.log_polling = False
        GLib.timeout_add(250, self.poll_log)

    def completed(self, process, result, context):
        path, plan, log = context
        process.wait_finish(result)
        code = process.get_exit_status() if process.get_if_exited() else -process.get_term_sig()
        self.process = None
        self.last_result = code
        self.activity_title = (
            f"Application finished: exit {code}"
            if code >= 0
            else f"Application ended by signal {-code}"
        )

        def finish():
            receipt_error = None
            if plan["installer"]:
                try:
                    balls.native_request(
                        {
                            "action": "record-install",
                            "ball": str(path),
                            "receipt": plan["receipt"],
                            "programHost": plan["programHost"],
                            "exitCode": code,
                        }
                    )
                except (OSError, ValueError) as error:
                    receipt_error = str(error)
            return (
                balls.native_request({"action": "snapshot", "ball": str(path)}),
                self.read_log(log),
                receipt_error,
                self.read_receipt(path),
            )

        def finished(result):
            self.snapshot, self.log_text, error, self.install_summary = result
            self.render()
            self.show_page("activity")
            if error:
                self.report_error("Installer receipt could not be saved: " + error)
            self.toast(self.activity_title)

        self.set_busy(False)
        self.request_work(finish, finished)

    @staticmethod
    def read_log(path):
        if not path or not path.exists():
            return ""
        with path.open("rb") as log:
            log.seek(max(0, path.stat().st_size - 65536))
            return log.read().decode("utf-8", errors="replace").replace("\x00", "")

    def update_activity(self):
        if hasattr(self, "status"):
            self.status.set_label(self.activity_title)
            self.log_view.get_buffer().set_text(self.log_text)

    def poll_log(self):
        if self.process and not self.log_polling:
            self.log_polling = True
            path = self.last_log

            def updated(text):
                self.log_polling = False
                if self.last_log == path:
                    self.log_text = text
                    self.update_activity()

            self.request_work(lambda: self.read_log(path), updated, locked=False)
        return self.process is not None

    def about(self):
        Adw.AboutDialog(
            application_name="2ine Balls",
            application_icon=APP_ID,
            developer_name="2ine Contributors",
            version="0.1.0",
            website="https://github.com/bitwisecook/2ine",
            license_type=Gtk.License.ZLIB,
            comments="OS/2 applications, comfortably at home on GNOME.",
        ).present(self)

    def open_path(self, path):
        try:
            Gio.AppInfo.launch_default_for_uri(path.as_uri(), None)
        except GLib.Error as error:
            self.toast(error.message)


class BallApplication(Adw.Application):
    def __init__(self, build=None, ball=None):
        super().__init__(application_id=APP_ID, flags=Gio.ApplicationFlags.HANDLES_OPEN)
        self.build = build
        self.initial = ball
        quit_action = Gio.SimpleAction.new("quit", None)
        quit_action.connect(
            "activate",
            lambda *_: (
                self.get_active_window().close() if self.get_active_window() else self.quit()
            ),
        )
        self.add_action(quit_action)
        self.set_accels_for_action("app.quit", ["<Control>q"])
        self.set_resource_base_path("/io/github/bitwisecook/Twoine/Balls")

    def do_open(self, files, _count, _hint):
        self.do_activate()
        path = files[0].get_path() if files else None
        if path:
            window = self.get_active_window()

            def opened():
                if window.busy:
                    return GLib.SOURCE_CONTINUE
                window.remember(Path(path))
                return GLib.SOURCE_REMOVE

            GLib.timeout_add(50, opened)

    def do_activate(self):
        window = self.get_active_window()
        if window is None:
            window = BallWindow(self, self.build, self.initial)
        window.present()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, help="2ine build directory containing lx_loader")
    parser.add_argument("--ball", type=Path, help="open an existing ball")
    parser.add_argument("files", nargs="*", type=Path)
    args = parser.parse_args()
    files = ([args.ball] if args.ball else []) + args.files
    return BallApplication(args.build).run([sys.argv[0], *(str(path.resolve()) for path in files)])


if __name__ == "__main__":
    raise SystemExit(main())
